/*
 [auto_generated]
 libs/numeric/odeint/test_external/eigen/runge_kutta4.cpp

 [begin_description]
 tba.
 [end_description]

 Copyright 2013 Karsten Ahnert
 Copyright 2013 Mario Mulansky

 Distributed under the Boost Software License, Version 1.0.
 (See accompanying file LICENSE_1_0.txt or
 copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#ifdef BOOST_MSVC
#pragma warning(disable : 4996)
#endif

#include <iostream>
#include "odesolver.h"
#include "utilityio.h"

using namespace boost::numeric::odeint;

struct sys {
  template <class State, class Deriv>
  void operator()(const State &x, Deriv &dxdt, double t) const {
    dxdt[0] = -K / M * x[1];
    dxdt[1] = x[0];
  }

  void setK(double _K) { K = _K; }

  double K = 1;
  double M = 1;
};

int main() {
  typedef Eigen::Matrix<double, 2, 1> state_type;
  state_type x;
  x[0] = 0.0;
  x[1] = 3.0;

  runge_kutta4<state_type, double, state_type, double, vector_space_algebra>
      rk4;

  int total_step = 100;
  Eigen::MatrixXd save_x(total_step, 2);

  for (int i = 0; i != 100; ++i) {
    rk4.do_step(sys(), x, 0.0, 0.1);
    save_x.row(i) = x.transpose();
  }
  utilityio _utilityio;
  _utilityio.write2csvfile("csvfile.csv", vvv);
}