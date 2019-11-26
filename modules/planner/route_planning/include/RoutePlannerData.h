
/*
*******************************************************************************
* RoutePlannerData.h:
* define the data struct used in the route planner
* This header file can be read by C++ compilers
*
* by Hu.ZH(CrossOcean.ai)
*******************************************************************************
*/

#ifndef _ROUTEPLANNERDATA_H_
#define _ROUTEPLANNERDATA_H_

#include <common/math/eigen/Eigen/Core>
#include <common/math/eigen/Eigen/Dense>
#include <vector>
#include "common/property/include/vesseldata.h"

namespace ASV::planning {

// real time data in planner
struct RoutePlannerRTdata {
  double speed;  // desired speed forward
  double los_capture_radius;

  Eigen::VectorXd Waypoint_X;          // N in the UTM coordinate
  Eigen::VectorXd Waypoint_Y;          // E in the UTM coordinate
  Eigen::VectorXd Waypoint_longitude;  // longitude
  Eigen::VectorXd Waypoint_latitude;   // latitude
};

}  // namespace ASV::planning

#endif /*_ROUTEPLANNERDATA_H_*/
