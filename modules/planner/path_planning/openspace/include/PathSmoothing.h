/*
***********************************************************************
* PathSmoothing.h:
* improve the smoothness of path, using conjugate-gradeient descent
* This header file can be read by C++ compilers
*
* by Hu.ZH(CrossOcean.ai)
***********************************************************************
*/

#ifndef _PATHSMOOTHING_H_
#define _PATHSMOOTHING_H_

#include <iostream>
#include "CollisionChecking.h"

namespace ASV::planning {

class PathSmoothing {
  using vec2d = ASV::common::math::Vec2d;
  using vec4t = std::vector<std::tuple<double, double, double, bool>>;
  using vec3t = std::vector<std::tuple<vec2d, bool>>;

 public:
  explicit PathSmoothing(const SmootherConfig &smootherconfig)
      : dmax_(smootherconfig.d_max),
        kappa_max_(0.3),
        fine_discrete_(0.05),
        x_resolution_(0.01),
        y_resolution_(0.01),
        theta_resolution_(0.01),
        omega_o_(0.1),
        omega_k_(1),
        omega_s_(1),
        start_state_({0, 0, 0}),
        end_state_({0, 0, 0}) {}
  virtual ~PathSmoothing() = default;

  PathSmoothing &SetupCoarsePath(const vec4t &path) {
    // TODO: assert
    assert(path.size() > 3);
    // setup the start/end heading
    start_state_ = {std::get<0>(path.front()), std::get<1>(path.front()),
                    std::get<2>(path.front())};
    end_state_ = {std::get<0>(path.back()), std::get<1>(path.back()),
                  std::get<2>(path.back())};
    // remove the closed vertex and find the forward/reverse switch vertex
    FindForwardReverseSwitch(path, coarse_vec2d_, coarse_isforward_);

    return *this;
  }  // SetupCoarsePath

  PathSmoothing &PerformSmoothing(
      const CollisionChecking_Astar &collision_checker) {
    smooth_path_ = coarse_vec2d_;
    for (std::size_t seg = 0; seg != coarse_vec2d_.size(); seg++) {
      smooth_path_[seg] =
          OneSegmentSmoothing(collision_checker, coarse_vec2d_[seg], dmax_,
                              kappa_max_, omega_o_, omega_s_, omega_k_);
    }
    return *this;
  }  // PerformSmoothing

  // trajectory interpolation
  PathSmoothing &InterpolateTrajectory() {
    std::vector<std::vector<vec2d>> smooth_path_test;

    for (const auto &segment : smooth_path_) {
      // super sample
      auto oneseg_fine_path = SuperSample(segment);
      // hold the orignal vertex and minimize the curvature

      GenerateCurvatureTerm(oneseg_fine_path);

      std::vector<vec2d> smoothing_segment;
      for (const auto &value : oneseg_fine_path) {
        smoothing_segment.push_back(std::get<0>(value));
      }

      smooth_path_test.push_back(smoothing_segment);
    }

    fine_path_ = GenerateFineTrajectory(smooth_path_test, coarse_isforward_);

    return *this;
  }  // InterpolateTrajectory

  auto coarse_vec2d() const noexcept { return coarse_vec2d_; }
  auto coarse_isforward() const noexcept { return coarse_isforward_; }
  auto smooth_path() const noexcept { return smooth_path_; }
  auto fine_path() const noexcept { return fine_path_; }

 private:
  const double dmax_;              // m
  const double kappa_max_;         // 1/m
  const double fine_discrete_;     // m
  const double x_resolution_;      // m
  const double y_resolution_;      // m
  const double theta_resolution_;  // rad
  const double omega_o_;           // penality of obstacle term
  const double omega_k_;           // penality of curvature term
  const double omega_s_;           // penality of smoothing term

  std::array<double, 3> start_state_;
  std::array<double, 3> end_state_;

  // coarse vertex and index of forward/reverse switch point in coarse path
  mutable std::vector<std::vector<vec2d>> coarse_vec2d_;
  mutable std::vector<bool> coarse_isforward_;

  // fine path
  mutable std::vector<std::vector<vec2d>> smooth_path_;
  mutable std::vector<std::array<double, 3>> fine_path_;

  // perform path smoothing on one segment
  std::vector<vec2d> OneSegmentSmoothing(
      const CollisionChecking_Astar &collision_checker,
      const std::vector<vec2d> &coarse_path, const double dmax,
      const double kappa_max, const double omega_o, const double omega_s,
      const double omega_k) {
    auto smooth_path = coarse_path;
    if (coarse_path.size() >= 3) {  // ensure the size
      for (int i = 0; i != 1; ++i) {
        std::vector<std::vector<vec2d>> _nearest_obstacles =
            GenerateNearestNeighbors(collision_checker, coarse_path, dmax);

        // compute the gradient of cost function
        std::vector<vec2d> _gradient =
            GenerateGradient(_nearest_obstacles, coarse_path, dmax, kappa_max,
                             omega_o, omega_s, omega_k);

        double _norm2_gradient = ComputeL2NormSquare(_gradient);

        // backtracking line search
        double _alpha = 0.2;
        double _beta = 0.5;
        double _gamma = 2;  // 0 < gamma

        double _cost = GenerateCost(_nearest_obstacles, coarse_path, dmax,
                                    kappa_max, omega_o, omega_s, omega_k);

        int count = 0;

        do {
          count++;
          // update the coarse path
          _gamma *= _beta;
          smooth_path = UpdateTrajectory(coarse_path, _gradient, _gamma);
        } while (GenerateCost(_nearest_obstacles, smooth_path, dmax, kappa_max,
                              omega_o, omega_s, omega_k) >=
                 _cost - _alpha * _gamma * _norm2_gradient);

        std::cout << "count " << count << std::endl;
      }
    }
    return smooth_path;

  }  // OneSegmentSmoothing

  // compute the nearest neighbors of each vertex on one segment
  std::vector<std::vector<vec2d>> GenerateNearestNeighbors(
      const CollisionChecking_Astar &collision_checker,
      const std::vector<vec2d> &path, const double dmax) const {
    std::size_t size_coarse_path = path.size();
    std::vector<std::vector<vec2d>> _nearest_obstacles(size_coarse_path,
                                                       {{0, 0}});
    // gradient at start/end and fixed node are all zero
    for (std::size_t index = 1; index != (size_coarse_path - 1); ++index) {
      // find the nearest neighbors around the current node
      auto i_nearest_obstacles = collision_checker.FindNearestNeighbors(
          path[index].x(), path[index].y(), dmax);
      _nearest_obstacles[index] = i_nearest_obstacles;
    }
    return _nearest_obstacles;
  }  // GenerateNearestNeighbors

  // compute the gradient of each vertex on one segment
  std::vector<vec2d> GenerateGradient(
      const std::vector<std::vector<vec2d>> &nearest_obstacles,
      const std::vector<vec2d> &path, const double dmax, const double kappa_max,
      const double omega_o, const double omega_s, const double omega_k) const {
    std::size_t size_coarse_path = path.size();

    // compute the gradient of cost function
    std::vector<vec2d> _gradient(size_coarse_path, {0, 0});

    //
    std::vector<vec2d> _Xim1_Xi_Xip1(size_coarse_path, {0, 0});
    std::vector<vec2d> P1i(size_coarse_path, {0, 0});
    std::vector<vec2d> P2i(size_coarse_path, {0, 0});
    std::vector<double> Delta_Thetai(size_coarse_path, 0.0);

    for (std::size_t index = 1; index != (size_coarse_path - 1); ++index) {
      auto Xi_Xim1 = path[index] - path[index - 1];
      auto Xip1_Xi = path[index + 1] - path[index];
      auto n_Xip1_Xi = Xip1_Xi * (-1);

      auto _P1i = Xi_Xim1.OrthogonalComplementNormal(n_Xip1_Xi);
      auto _P2i = n_Xip1_Xi.OrthogonalComplementNormal(Xi_Xim1);
      double _Delta_Thetai = vec2d::AngleTwoVectors(Xi_Xim1, Xip1_Xi);

      double _reciprocal_length = 1.0 / Xi_Xim1.Length();
      double _Delta_Thetai_sin = std::sin(_Delta_Thetai);
      double _reciprocal_length_sin = 0;
      if (_Delta_Thetai_sin <= theta_resolution_)
        _reciprocal_length_sin = _reciprocal_length / theta_resolution_;
      else
        _reciprocal_length_sin = _reciprocal_length / _Delta_Thetai_sin;

      _Xim1_Xi_Xip1[index] = Xip1_Xi - Xi_Xim1;
      P1i[index] = _P1i * _reciprocal_length_sin;
      P2i[index] = _P2i * _reciprocal_length_sin;
      Delta_Thetai[index] = _Delta_Thetai * _reciprocal_length;
    }

    // gradient at start/end and fixed node are all zero
    for (std::size_t index = 1; index != (size_coarse_path - 1); ++index) {
      // gradient obstacle
      vec2d i_gradient_obstacle(0, 0);
      for (const auto &nearest_obstacle : nearest_obstacles[index]) {
        // add the obstacle potential in the nearst neighbors
        auto x2o = path[index] - nearest_obstacle;
        // assume the x2o is not very small
        double k = 1.0 - dmax / x2o.Length();
        i_gradient_obstacle += (x2o * k);
      }

      // gradient smoothing
      vec2d i_gradient_smooth = _Xim1_Xi_Xip1[index - 1] +
                                _Xim1_Xi_Xip1[index + 1] -
                                _Xim1_Xi_Xip1[index] * 2;

      // gradient curvature
      auto Xi_Xim1 = path[index] - path[index - 1];
      auto Xip1_Xi = path[index + 1] - path[index];

      vec2d i_gradient_curvature =
          (Xip1_Xi * (Delta_Thetai[index + 1] / Xip1_Xi.LengthSquare()) -
           P2i[index + 1]) *
              (Delta_Thetai[index + 1] - kappa_max) +
          (P1i[index] + P2i[index] -
           Xi_Xim1 * (Delta_Thetai[index] / Xi_Xim1.LengthSquare())) *
              (Delta_Thetai[index] - kappa_max) -
          P1i[index - 1] * (Delta_Thetai[index - 1] - kappa_max);

      // add all gradients
      _gradient[index] = i_gradient_smooth * omega_s +
                         i_gradient_obstacle * omega_o +
                         i_gradient_curvature * omega_k;

    }  // end for loop
    return _gradient;
  }  // GenerateGradient

  // compute the cost value given the current trajectory
  double GenerateCost(const std::vector<std::vector<vec2d>> &nearest_obstacles,
                      const std::vector<vec2d> &path, const double dmax,
                      const double kappa_max, const double omega_o,
                      const double omega_s, const double omega_k) const {
    double _obstacle_cost = 0.0;
    double _smooth_cost = 0.0;
    double _curvature_cost = 0.0;

    std::size_t size_coarse_path = path.size();

    // gradient at start/end and fixed node are all zero
    for (std::size_t index = 1; index != (size_coarse_path - 1); ++index) {
      // cost obstacle
      double i_obstacle = 0;
      for (const auto &nearest_obstacle : nearest_obstacles[index]) {
        // add the obstacle potential in the nearst neighbors
        auto x2o = path[index] - nearest_obstacle;
        i_obstacle += std::pow(dmax - x2o.Length(), 2);
      }
      _obstacle_cost += i_obstacle;

      // cost smoothing
      auto Xi_Xim1 = path[index] - path[index - 1];
      auto Xip1_Xi = path[index + 1] - path[index];
      auto i_Xim1_Xi_Xip1 = Xip1_Xi - Xi_Xim1;
      _smooth_cost += i_Xim1_Xi_Xip1.LengthSquare();

      // cost curvature
      double _Delta_Thetai =
          vec2d::AngleTwoVectors(Xi_Xim1, Xip1_Xi) / Xi_Xim1.Length();
      _curvature_cost += std::pow(_Delta_Thetai - kappa_max, 2);
    }  // end for loop
    return _obstacle_cost * omega_o + _smooth_cost * omega_s +
           _curvature_cost * omega_k;

  }  // GenerateCost

  // update the coarse path
  std::vector<vec2d> UpdateTrajectory(const std::vector<vec2d> &path,
                                      const std::vector<vec2d> &gradient,
                                      const double gamma) const {
    auto new_path = path;
    for (std::size_t index = 0; index != new_path.size(); ++index)
      new_path[index] -= (gradient[index] * gamma);
    return new_path;
  }  // UpdateTrajectory

  // super sample the smooth path, with smooth vertex fixed
  vec3t SuperSample(const std::vector<vec2d> &smoothpath) const {
    vec3t fine_path;

    std::size_t size_smooth_path = smoothpath.size();
    for (std::size_t index = 0; index != (size_smooth_path - 1); ++index) {
      auto i_smooth_path = smoothpath[index];
      // generate the fixed vertex
      fine_path.push_back({i_smooth_path, true});
      // linear intepolation
      auto diff_vec2d = smoothpath[index + 1] - i_smooth_path;
      double diff_length = diff_vec2d.Length();
      for (double j = fine_discrete_; j < diff_length; j += fine_discrete_) {
        fine_path.push_back(
            {i_smooth_path + diff_vec2d * (j / diff_length), false});
      }
    }
    // don't forget the last one
    fine_path.push_back({smoothpath.back(), true});

    return fine_path;
  }  // SuperSample

  // generate the fine trajectory from the fine path
  std::vector<std::array<double, 3>> GenerateFineTrajectory(
      const std::vector<std::vector<vec2d>> &fine_path,
      const std::vector<bool> &isforward) const {
    std::vector<std::array<double, 3>> fine_trajectory;

    for (std::size_t seg = 0; seg != fine_path.size(); ++seg) {
      auto segment_fine_path = fine_path[seg];
      bool segment_isforward = isforward[seg];
      for (std::size_t index = 0; index != (segment_fine_path.size() - 1);
           ++index) {
        vec2d delta_vec2d(0, 0);
        if (segment_isforward)
          delta_vec2d = segment_fine_path[index + 1] - segment_fine_path[index];
        else
          delta_vec2d = segment_fine_path[index] - segment_fine_path[index + 1];
        double heading = delta_vec2d.Angle();
        fine_trajectory.push_back({segment_fine_path[index].x(),
                                   segment_fine_path[index].y(), heading});
      }
    }

    return fine_trajectory;
  }  // GenerateFineTrajectory

  // compute the gradient of obstacle term
  std::vector<vec2d> GenerateObstacleTerm(
      const CollisionChecking_Astar &collision_checker, const vec3t &path) {
    std::size_t total_num = path.size();
    std::vector<vec2d> obstacleTerm(total_num, {0, 0});

    // obstacleTerm at start/end and fixed node are all zero
    for (std::size_t index = 1; index != (total_num - 1); ++index) {
      if (!std::get<1>(path[index])) {  // not fixed node
        vec2d current_obstacle_term(0, 0);
        auto current_state_vec2d = std::get<0>(path[index]);

        // find the nearest neighbors around the current node
        auto nearest_obstacles = collision_checker.FindNearestNeighbors(
            current_state_vec2d.x(), current_state_vec2d.y(), dmax_);

        // add the obstacle potential in the nearst neighbors
        for (const auto &nearest_obstacle : nearest_obstacles) {
          auto x2o = current_state_vec2d -
                     vec2d(nearest_obstacle.x(), nearest_obstacle.y());
          // assume the x2o is not very small
          double k = 2 * omega_o_ * (1.0 - dmax_ / x2o.Length());
          current_obstacle_term += (x2o * k);
        }

        obstacleTerm[index] = current_obstacle_term;
      }  // end if
    }    // end for loop
    return obstacleTerm;
  }  // GenerateObstacleTerm

  // compute the gradient of smoothing term
  std::vector<vec2d> GenerateSmoothTerm(const vec3t &path) {
    std::size_t total_num = path.size();
    std::vector<vec2d> SmoothTerm(total_num, {0, 0});
    std::vector<vec2d> Xim1_Xi_Xip1(total_num, {0, 0});

    for (std::size_t index = 1; index != (total_num - 1); ++index) {
      Xim1_Xi_Xip1[index] = std::get<0>(path[index - 1]) +
                            std::get<0>(path[index + 1]) -
                            std::get<0>(path[index]) * 2;
    }

    // SmoothTerm at start/end and fixed node are all zero
    for (std::size_t index = 1; index != (total_num - 1); ++index) {
      if (!std::get<1>(path[index])) {  // not fixed node
        vec2d current_smooth_term = Xim1_Xi_Xip1[index - 1] +
                                    Xim1_Xi_Xip1[index + 1] -
                                    Xim1_Xi_Xip1[index] * 2;
        current_smooth_term *= (2 * omega_s_);
        SmoothTerm[index] = current_smooth_term;
      }  // end if
    }    // end for loops
    return SmoothTerm;

  }  // GenerateSmoothTerm

  // compute the gradient of curvature term ()
  std::vector<vec2d> GenerateCurvatureTerm(const vec3t &path) {
    std::size_t total_num = path.size();

    std::vector<vec2d> CurvatureTerm(total_num, {0, 0});
    std::vector<vec2d> P1i(total_num, {0, 0});
    std::vector<vec2d> P2i(total_num, {0, 0});
    std::vector<double> Delta_Thetai(total_num, 0.0);

    // prepare
    for (std::size_t index = 1; index != (total_num - 1); ++index) {
      auto Xi_Xim1 = std::get<0>(path[index]) - std::get<0>(path[index - 1]);
      auto Xip1_Xi = std::get<0>(path[index + 1]) - std::get<0>(path[index]);
      auto n_Xip1_Xi = Xip1_Xi * (-1);

      auto _P1i = Xi_Xim1.OrthogonalComplementNormal(n_Xip1_Xi);
      auto _P2i = n_Xip1_Xi.OrthogonalComplementNormal(Xi_Xim1);
      double _Delta_Thetai = vec2d::AngleTwoVectors(Xi_Xim1, Xip1_Xi);

      double _reciprocal_length = 1.0 / Xi_Xim1.Length();
      double _Delta_Thetai_sin = std::sin(_Delta_Thetai);
      double _reciprocal_length_sin = 0;
      if (_Delta_Thetai_sin <= theta_resolution_)
        _reciprocal_length_sin = _reciprocal_length / theta_resolution_;
      else
        _reciprocal_length_sin = _reciprocal_length / _Delta_Thetai_sin;

      P1i[index] = _P1i * _reciprocal_length_sin;
      P2i[index] = _P2i * _reciprocal_length_sin;
      Delta_Thetai[index] = _Delta_Thetai * _reciprocal_length;
    }

    // CurvatureTerm at start/end and fixed node are all zero
    for (std::size_t index = 1; index != (total_num - 1); ++index) {
      if (!std::get<1>(path[index])) {  // not fixed node

        auto Xi_Xim1 = std::get<0>(path[index]) - std::get<0>(path[index - 1]);
        auto Xip1_Xi = std::get<0>(path[index + 1]) - std::get<0>(path[index]);

        vec2d current_curvature_term =
            (Xip1_Xi * (Delta_Thetai[index + 1] / Xip1_Xi.LengthSquare()) -
             P2i[index + 1]) *
                Delta_Thetai[index + 1] +
            (P1i[index] + P2i[index] -
             Xi_Xim1 * (Delta_Thetai[index] / Xi_Xim1.LengthSquare())) *
                Delta_Thetai[index] -
            P1i[index - 1] * Delta_Thetai[index - 1];
        current_curvature_term *= (2 * omega_k_);
        CurvatureTerm[index] = current_curvature_term;
      }  // end if
    }    // end for loops

    return CurvatureTerm;
  }  // GenerateCurvatureTerm

  // find the trajectory node when the forward/reverse switch occurs
  void FindForwardReverseSwitch(const vec4t &coarse_trajectory,
                                std::vector<std::vector<vec2d>> &coarse_vec2d,
                                std::vector<bool> &coarse_isforward) const {
    vec4t processed_coarse_trajectory;

    coarse_vec2d.clear();
    coarse_isforward.clear();

    // start point
    processed_coarse_trajectory.push_back(coarse_trajectory[0]);

    coarse_vec2d.push_back({vec2d(std::get<0>(coarse_trajectory[0]),
                                  std::get<1>(coarse_trajectory[0]))});
    coarse_isforward.push_back(std::get<3>(coarse_trajectory[0]));

    for (const auto &current_state : coarse_trajectory) {
      auto processed_coarse_last = processed_coarse_trajectory.back();
      if (IsSameNode(std::get<0>(processed_coarse_last),
                     std::get<1>(processed_coarse_last),
                     std::get<2>(processed_coarse_last),
                     std::get<0>(current_state), std::get<1>(current_state),
                     std::get<2>(current_state)) &&
          (std::get<3>(processed_coarse_last) ==
           std::get<3>(current_state))) {  // same state
        continue;
      }

      if (std::get<3>(processed_coarse_last) != std::get<3>(current_state)) {
        // new a list of vertex
        processed_coarse_trajectory.push_back(current_state);
        coarse_vec2d.push_back(
            {vec2d(std::get<0>(current_state), std::get<1>(current_state))});
        coarse_isforward.push_back(std::get<3>(current_state));
      } else {
        // continue to push_back the node on the current trajectory
        processed_coarse_trajectory.push_back(current_state);
        coarse_vec2d.back().push_back(
            vec2d(std::get<0>(current_state), std::get<1>(current_state)));
      }
    }
  }  // FindForwardReverseSwitch

  // check if two node is closed
  bool IsSameNode(const double lhs_x, const double lhs_y,
                  const double lhs_theta, const double rhs_x,
                  const double rhs_y, const double rhs_theta) const {
    return ((std::abs(lhs_x - rhs_x) <= x_resolution_) &&
            (std::abs(lhs_y - rhs_y) <= y_resolution_) &&
            (std::abs(ASV::common::math::Normalizeheadingangle(
                 lhs_theta - rhs_theta)) <= theta_resolution_));
  }  // IsSameNode

  double ComputeL2NormSquare(const std::vector<vec2d> &x) {
    double l2norm = 0;
    for (const auto &v : x) l2norm += v.LengthSquare();
    return l2norm;
  }  // ComputeL2NormSquare

};  // end class PathSmoothing
}  // namespace ASV::planning

#endif /* _PATHSMOOTHING_H_ */