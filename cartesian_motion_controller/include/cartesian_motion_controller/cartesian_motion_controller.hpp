////////////////////////////////////////////////////////////////////////////////
// Copyright 2019 FZI Research Center for Information Technology
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
/*!\file    cartesian_motion_controller.hpp
 *
 * \author  Stefan Scherzinger <scherzin@fzi.de>
 * \date    2017/07/27
 *
 */
//-----------------------------------------------------------------------------

#ifndef CARTESIAN_MOTION_CONTROLLER_HPP_INCLUDED
#define CARTESIAN_MOTION_CONTROLLER_HPP_INCLUDED

// Project
#include <cartesian_motion_controller/cartesian_motion_controller.h>

// Other
#include <boost/algorithm/clamp.hpp>

namespace cartesian_motion_controller
{

template <class HardwareInterface>
CartesianMotionController<HardwareInterface>::
CartesianMotionController()
: Base::CartesianControllerBase()
{
}

template <class HardwareInterface>
bool CartesianMotionController<HardwareInterface>::
init(HardwareInterface* hw, ros::NodeHandle& nh)
{
  Base::init(hw,nh);

  // Get namespace
  m_ns = nh.getNamespace().substr(0, nh.getNamespace().find("/", 2));

  if (!nh.getParam("target_frame_topic",m_target_frame_topic))
  {
    m_target_frame_topic = "robot_goal";
    ROS_WARN_STREAM("Failed to load "
        << nh.getNamespace() + "/target_frame_topic"
        << " from parameter server. Will default to: "
        << nh.getNamespace() + '/' + m_target_frame_topic);
  }

  m_target_frame_subscr = nh.subscribe(
      m_ns+"/"+m_target_frame_topic,
      3,
      &CartesianMotionController<HardwareInterface>::targetFrameCallback,
      this);

  // FrankaState Publisher 
  m_frankaState_publisher = nh.advertise<franka_msgs::FrankaState>(m_ns + "/franka_state_controller/franka_states",10);
  // jointState Publisher 
  m_jointStates_publisher = nh.advertise<sensor_msgs::JointState>(m_ns + "/franka_state_controller/joint_states",10);
  // jointState Subscriber
  m_jointStates_subscriber = nh.subscribe(
      m_ns+"/joint_states",
      3,
      &CartesianMotionController<HardwareInterface>::jointStatesCallback,
      this);

  return true;
}

template <class HardwareInterface>
void CartesianMotionController<HardwareInterface>::
starting(const ros::Time& time)
{
  // Reset simulation with real joint state
  Base::starting(time);
  m_current_frame = Base::m_forward_dynamics_solver.getEndEffectorPose();

  // Start where we are
  m_target_frame = m_current_frame;
}

template <class HardwareInterface>
void CartesianMotionController<HardwareInterface>::
stopping(const ros::Time& time)
{
}

template <class HardwareInterface>
void CartesianMotionController<HardwareInterface>::
update(const ros::Time& time, const ros::Duration& period)
{
  // X   Y   Z   Translation
  // Xx  Yy  Zx  x
  // Xy  Yx  Zy  y
  // Xz  Yz  Zz  z 
  // m3  m7  m11 m15
  
  //////////////////////////////////////////////////////////////////  
  //  Update O_T_EE : Measured end effector pose in base frame.   //
  //  Pose is represented as a 4x4 matrix in column-major format. //
  //////////////////////////////////////////////////////////////////
  
  // get End Effector pose
  m_current_frame = Base::m_forward_dynamics_solver.getEndEffectorPose();
  
  // update position
  m_frankaState.O_T_EE[12] = m_current_frame.p.x();
  m_frankaState.O_T_EE[13] = m_current_frame.p.y();
  m_frankaState.O_T_EE[14] = m_current_frame.p.z();

  m_frankaState.O_T_EE[0] = m_current_frame.M.Inverse().data[0];
  m_frankaState.O_T_EE[1] = m_current_frame.M.Inverse().data[3];
  m_frankaState.O_T_EE[2] = m_current_frame.M.Inverse().data[6];

  m_frankaState.O_T_EE[4] = m_current_frame.M.Inverse().data[1];
  m_frankaState.O_T_EE[5] = m_current_frame.M.Inverse().data[4];
  m_frankaState.O_T_EE[6] = m_current_frame.M.Inverse().data[7];

  m_frankaState.O_T_EE[8] = m_current_frame.M.Inverse().data[2];
  m_frankaState.O_T_EE[9] = m_current_frame.M.Inverse().data[5];
  m_frankaState.O_T_EE[10] = m_current_frame.M.Inverse().data[8];

  //////////////////////////////////////////////////////////////////  
  //  Update F_T_EE : End effector frame pose in flange frame.    //
  //  Pose is represented as a 4x4 matrix in column-major format. //
  //////////////////////////////////////////////////////////////////
  
  // update position
  m_frankaState.F_T_EE[12] =  1.000;
  m_frankaState.F_T_EE[13] =  2.000;
  m_frankaState.F_T_EE[14] =  3.000;

  //////////////////////////////////////////////////////////////////  
  //  Update EE_T_K : Stiffness frame pose in end effector frame. //
  //  Pose is represented as a 4x4 matrix in column-major format. //
  //////////////////////////////////////////////////////////////////
  
  // update position
  m_frankaState.EE_T_K[12] =  1.000;
  m_frankaState.EE_T_K[13] =  2.000;
  m_frankaState.EE_T_K[14] =  3.000;

  // frankaState publisher
  m_frankaState_publisher.publish(m_frankaState);

  // Forward Dynamics turns the search for the according joint motion into a
  // control process. So, we control the internal model until we meet the
  // Cartesian target motion. This internal control needs some simulation time
  // steps.
  for (int i = 0; i < Base::m_iterations; ++i)
  {
    // The internal 'simulation time' is deliberately independent of the outer
    // control cycle.
    ros::Duration internal_period(0.02);

    // Compute the motion error = target - current.
    ctrl::Vector6D error = computeMotionError();

    // Turn Cartesian error into joint motion
    Base::computeJointControlCmds(error,internal_period);
  }

  // Write final commands to the hardware interface
  Base::writeJointControlCmds();

}

template <>
void CartesianMotionController<hardware_interface::VelocityJointInterface>::
update(const ros::Time& time, const ros::Duration& period)
{
  // Simulate only one step forward to avoid drift.
  ros::Duration internal_period(0.02);

  ctrl::Vector6D error = computeMotionError();

  Base::computeJointControlCmds(error,internal_period);

  Base::writeJointControlCmds();
}

template <class HardwareInterface>
ctrl::Vector6D CartesianMotionController<HardwareInterface>::
computeMotionError()
{
  // Compute motion error wrt robot_base_link
  m_current_frame = Base::m_forward_dynamics_solver.getEndEffectorPose();

  // Transformation from target -> current corresponds to error = target - current
  KDL::Frame error_kdl;
  error_kdl.M = m_target_frame.M * m_current_frame.M.Inverse();
  error_kdl.p = m_target_frame.p - m_current_frame.p;

  // Use Rodrigues Vector for a compact representation of orientation errors
  // Only for angles within [0,Pi)
  KDL::Vector rot_axis = KDL::Vector::Zero();
  double angle    = error_kdl.M.GetRotAngle(rot_axis);   // rot_axis is normalized
  double distance = error_kdl.p.Normalize();

  // Clamp maximal tolerated error.
  // The remaining error will be handled in the next control cycle.
  const double max_angle = 1.0;
  const double max_distance = 1.0;
  angle    = boost::algorithm::clamp(angle,-max_angle,max_angle);
  distance = boost::algorithm::clamp(distance,-max_distance,max_distance);

  // Scale errors to allowed magnitudes
  rot_axis = rot_axis * angle;
  error_kdl.p = error_kdl.p * distance;

  // Reassign values
  ctrl::Vector6D error;
  error(0) = error_kdl.p.x();
  error(1) = error_kdl.p.y();
  error(2) = error_kdl.p.z();
  error(3) = rot_axis(0);
  error(4) = rot_axis(1);
  error(5) = rot_axis(2);

  return error;
}

template <class HardwareInterface>
void CartesianMotionController<HardwareInterface>::
targetFrameCallback(const geometry_msgs::PoseStamped& target)
{
  if (target.header.frame_id != Base::m_robot_base_link)
  {
    ROS_WARN_STREAM_THROTTLE(3, "Got target pose in wrong reference frame. Expected: "
        << Base::m_robot_base_link << " but got "
        << target.header.frame_id);
    return;
  }

  m_target_frame = KDL::Frame(
      KDL::Rotation::Quaternion(
        target.pose.orientation.x,
        target.pose.orientation.y,
        target.pose.orientation.z,
        target.pose.orientation.w),
      KDL::Vector(
        target.pose.position.x,
        target.pose.position.y,
        target.pose.position.z));
}

template <class HardwareInterface>
void CartesianMotionController<HardwareInterface>::
jointStatesCallback(const sensor_msgs::JointState& joint_states)
{
  m_jointStates_publisher.publish(joint_states);
}

} // namespace

#endif
