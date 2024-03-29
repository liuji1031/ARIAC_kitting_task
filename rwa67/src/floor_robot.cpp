#include "floor_robot.hpp"
#include "utils.hpp"

FloorRobot::FloorRobot()
    : Node("floor_robot_node"),
      node_(std::make_shared<rclcpp::Node>("example_group_node")),
      executor_(std::make_shared<rclcpp::executors::MultiThreadedExecutor>()),
      planning_scene_()
{

    auto mgi_options = moveit::planning_interface::MoveGroupInterface::Options(
        "floor_robot",
        "robot_description");

    floor_robot_ = std::make_shared<moveit::planning_interface::MoveGroupInterface>(node_, mgi_options);
    if (floor_robot_->startStateMonitor())
    {
        RCLCPP_INFO(this->get_logger(), "Floor Robot State Monitor Started");
    }
    else
    {
        RCLCPP_ERROR(this->get_logger(), "Floor Robot State Monitor Failed to Start");
    }

    // use upper joint velocity and acceleration limits
    floor_robot_->setMaxAccelerationScalingFactor(1.0);
    floor_robot_->setMaxVelocityScalingFactor(1.0);

    // callback groups
    rclcpp::SubscriptionOptions options;
    rclcpp::SubscriptionOptions gripper_options;
    subscription_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    gripper_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    server_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    options.callback_group = subscription_cbg_;
    gripper_options.callback_group = gripper_cbg_;

        // subscriber callback to /rwa67/floor_robot/go_home topic
    rwa67_sub_ = this->create_subscription<std_msgs::msg::String>(
            "/rwa67/floor_robot/go_home", 10,
            std::bind(&FloorRobot::floor_robot_sub_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/orders
    orders_sub_ = this->create_subscription<ariac_msgs::msg::Order>("/ariac/orders", 1,
                                                                    std::bind(&FloorRobot::orders_cb, this, std::placeholders::_1), options);
    // subscription to /ariac/competition_state
    competition_state_sub_ = this->create_subscription<ariac_msgs::msg::CompetitionState>(
        "/ariac/competition_state", 1,
        std::bind(&FloorRobot::competition_state_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/sensors/kts1_camera/image
    kts1_camera_sub_ = this->create_subscription<ariac_msgs::msg::AdvancedLogicalCameraImage>(
        "/ariac/sensors/kts1_camera/image", rclcpp::SensorDataQoS(),
        std::bind(&FloorRobot::kts1_camera_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/sensors/kts2_camera/image
    kts2_camera_sub_ = this->create_subscription<ariac_msgs::msg::AdvancedLogicalCameraImage>(
        "/ariac/sensors/kts2_camera/image", rclcpp::SensorDataQoS(),
        std::bind(&FloorRobot::kts2_camera_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/sensors/left_bins_camera/image
    left_bins_camera_sub_ = this->create_subscription<ariac_msgs::msg::AdvancedLogicalCameraImage>(
        "/ariac/sensors/left_bins_camera/image", rclcpp::SensorDataQoS(),
        std::bind(&FloorRobot::left_bins_camera_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/sensors/right_bins_camera/image
    right_bins_camera_sub_ = this->create_subscription<ariac_msgs::msg::AdvancedLogicalCameraImage>(
        "/ariac/sensors/right_bins_camera/image", rclcpp::SensorDataQoS(),
        std::bind(&FloorRobot::right_bins_camera_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/floor_robot_gripper/state
    floor_gripper_state_sub_ = this->create_subscription<ariac_msgs::msg::VacuumGripperState>(
        "/ariac/floor_robot_gripper_state", rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile(),
        std::bind(&FloorRobot::floor_gripper_state_cb, this, std::placeholders::_1), gripper_options);

    // subscription to /ariac/agv1_status
    agv1_status_sub_ = this->create_subscription<ariac_msgs::msg::AGVStatus>(
        "/ariac/agv1_status", 10,
        std::bind(&FloorRobot::agv1_status_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/agv2_status
    agv2_status_sub_ = this->create_subscription<ariac_msgs::msg::AGVStatus>(
        "/ariac/agv2_status", 10,
        std::bind(&FloorRobot::agv2_status_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/agv3_status
    agv3_status_sub_ = this->create_subscription<ariac_msgs::msg::AGVStatus>(
        "/ariac/agv3_status", 10,
        std::bind(&FloorRobot::agv3_status_cb, this, std::placeholders::_1), options);

    // subscription to /ariac/agv4_status
    agv4_status_sub_ = this->create_subscription<ariac_msgs::msg::AGVStatus>(
        "/ariac/agv4_status", 10,
        std::bind(&FloorRobot::agv4_status_cb, this, std::placeholders::_1), options);

    // client to /ariac/perform_quality_check
    quality_checker_ = this->create_client<ariac_msgs::srv::PerformQualityCheck>("/ariac/perform_quality_check");
    // client to /ariac/floor_robot_change_gripper
    floor_robot_tool_changer_ = this->create_client<ariac_msgs::srv::ChangeGripper>("/ariac/floor_robot_change_gripper");
    // client to /ariac/floor_robot_enable_gripper
    floor_robot_gripper_enable_ = this->create_client<ariac_msgs::srv::VacuumGripperControl>("/ariac/floor_robot_enable_gripper");

    // service to move the robot to home position
    move_robot_home_srv_ = create_service<std_srvs::srv::Trigger>(
        "/commander/move_robot_home",
        std::bind(
            &FloorRobot::move_robot_home_srv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    move_robot_to_table_srv_ = create_service<robot_commander_msgs::srv::MoveRobotToTable>(
        "/commander/move_robot_to_table",
        std::bind(
            &FloorRobot::move_robot_to_table_srv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    move_robot_to_tray_srv_ = create_service<robot_commander_msgs::srv::MoveRobotToTray>(
        "/commander/move_robot_to_tray",
        std::bind(
            &FloorRobot::move_robot_to_tray_srv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    pickup_part_srv_= create_service<custom_msgs::srv::PickupPart>(
            "/commander/pickup_part",
            std::bind(
            &FloorRobot::pickup_part_cb_, this, 
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default, server_cbg_);
        
    place_tray_on_agv_srv_ = create_service<custom_msgs::srv::PlacingTray>(
        "/commander/place_tray_on_agv",
        std::bind(
            &FloorRobot::place_tray_on_agv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    place_part_on_tray_srv_ = create_service<custom_msgs::srv::PlacingPart>(
        "/commander/place_part_on_tray",
        std::bind(
            &FloorRobot::place_part_on_tray_cb_, this,
            std::placeholders::_1, std::placeholders::_2),
            rmw_qos_profile_services_default,server_cbg_);

    // service to move the tray to the agv
    move_tray_to_agv_srv_ = create_service<robot_commander_msgs::srv::MoveTrayToAGV>(
        "/commander/move_tray_to_agv",
        std::bind(
            &FloorRobot::move_tray_to_agv_srv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    enter_tool_changer_srv_ = create_service<robot_commander_msgs::srv::EnterToolChanger>(
        "/commander/enter_tool_changer",
        std::bind(
            &FloorRobot::enter_tool_changer_srv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    exit_tool_changer_srv_ = create_service<robot_commander_msgs::srv::ExitToolChanger>(
        "/commander/exit_tool_changer",
        std::bind(
            &FloorRobot::exit_tool_changer_srv_cb_, this,
            std::placeholders::_1, std::placeholders::_2),rmw_qos_profile_services_default,server_cbg_);

    remove_part_srv_ = create_service<custom_msgs::srv::RemovePart>(
        "/commander/remove_part_from_agv",
        std::bind(&FloorRobot::remove_part_from_agv_srv_cb_, this,std::placeholders::_1, std::placeholders::_2),
        rmw_qos_profile_services_default,
        server_cbg_
    );

    // add models to the planning scene
    add_models_to_planning_scene_();

    executor_->add_node(node_);
    executor_thread_ = std::thread([this]()
                                   { this->executor_->spin(); });

    RCLCPP_INFO(this->get_logger(), "Initialization successful.");
    RCLCPP_INFO(this->get_logger(), "Waiting for Service calls.");
}

//=============================================//
FloorRobot::~FloorRobot()
{
    floor_robot_->~MoveGroupInterface();
}

//=============================================//
void FloorRobot::move_robot_home_srv_cb_(
    std_srvs::srv::Trigger::Request::SharedPtr request,
    std_srvs::srv::Trigger::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to move robot to home position");
    (void)request; // remove unused parameter warning

    if (go_home_())
    {
        response->success = true;
        response->message = "Robot moved to home";
    }
    else
    {
        response->success = false;
        response->message = "Unable to move robot to home";
    }
}

//=============================================//
bool FloorRobot::move_robot_home_()
{
    // Move floor robot to home joint state
    floor_robot_->setNamedTarget("home");
    return move_to_target_();
}

//=============================================//
void FloorRobot::move_robot_to_table_srv_cb_(
    robot_commander_msgs::srv::MoveRobotToTable::Request::SharedPtr request,
    robot_commander_msgs::srv::MoveRobotToTable::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to move robot to table");
    auto kts = request->kts;

    if (move_robot_to_table_(kts))
    {
        response->success = true;
        response->message = "Robot moved to table";
    }
    else
    {
        response->success = false;
        response->message = "Unable to move robot to table";
    }
}

//=============================================//
bool FloorRobot::move_robot_to_table_(int kts)
{
    if (kts == robot_commander_msgs::srv::MoveRobotToTable::Request::KTS1)
        floor_robot_->setJointValueTarget(floor_kts1_js_);
    else if (kts == robot_commander_msgs::srv::MoveRobotToTable::Request::KTS2)
        floor_robot_->setJointValueTarget(floor_kts2_js_);
    else
    {
        RCLCPP_ERROR(get_logger(), "Invalid table number");
        return false;
    }

    move_to_target_();
    return true;
}

//=============================================//
void FloorRobot::move_robot_to_tray_srv_cb_(
    robot_commander_msgs::srv::MoveRobotToTray::Request::SharedPtr request,
    robot_commander_msgs::srv::MoveRobotToTray::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to move robot to tray");
    auto tray_id = request->tray_id;
    auto tray_pose = request->tray_pose_in_world;

    if (move_robot_to_tray_(tray_id, tray_pose))
    {
        response->success = true;
        response->message = "Robot moved to tray";
    }
    else
    {
        response->success = false;
        response->message = "Unable to move robot to tray";
    }
}

void FloorRobot::pickup_part_cb_(
    custom_msgs::srv::PickupPart::Request::SharedPtr request,
    custom_msgs::srv::PickupPart::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to pick up a part");
    auto part_pose = request->part_pose;
    auto part_type = request->part_type;
    auto part_color = request->part_color;

    if (pickup_part(part_pose, part_type, part_color))
    {
        response->success = true;
        // response->message = "Part Picked up";
    }
    else
    {
        response->success = false;
        // response->message = "Unable to pick up part";
    }
}


bool FloorRobot::pickup_part(geometry_msgs::msg::Pose &part_pose_, int part_type_, int part_color_)
{
    double part_rotation = Utils::get_yaw_from_pose_(part_pose_);

    floor_robot_->setJointValueTarget("linear_actuator_joint", -part_pose_.position.y);
    move_to_target_();

    std::vector<geometry_msgs::msg::Pose> waypoints;
    part_rotation = 0.0;
    waypoints.push_back(Utils::build_pose(part_pose_.position.x, part_pose_.position.y,
                                          part_pose_.position.z + 0.5, set_robot_orientation_(part_rotation)));

    waypoints.push_back(Utils::build_pose(part_pose_.position.x, part_pose_.position.y,
                                          part_pose_.position.z + part_heights_[part_type_] + pick_offset_, set_robot_orientation_(part_rotation)));

    if (!move_through_waypoints_(waypoints, 0.3, 0.3))
    {
        RCLCPP_ERROR(get_logger(), "Unable to move robot above tray");
        return false;
    }

    wait_for_attach_completion_(5.0);
    if (floor_gripper_state_.attached)
    {
        // Add part to planning scene
        std::string part_name = part_colors_[part_color_] + "_" + part_types_[part_type_];
        // add_single_model_to_planning_scene_(part_name, part_types_[part_type_] + ".stl", part_pose_);
        floor_robot_->attachObject(part_name);

        auto part_to_pick = ariac_msgs::msg::Part();
        part_to_pick.type = part_type_;
        part_to_pick.color = part_color_;
        floor_robot_attached_part_ = part_to_pick;
        RCLCPP_INFO_STREAM(get_logger(), "Adding to the planning scene");

        // raise gripper
        waypoints.clear();
        waypoints.push_back(Utils::build_pose(part_pose_.position.x, part_pose_.position.y,
                                          part_pose_.position.z + 0.3, set_robot_orientation_(part_rotation)));
        move_through_waypoints_(waypoints, 0.3, 0.3);                        

        return true;
    }

    return false;
}

//=============================================//
void FloorRobot::place_tray_on_agv_cb_(
    custom_msgs::srv::PlacingTray::Request::SharedPtr request,
    custom_msgs::srv::PlacingTray::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to move robot to tray");
    auto tray_id = request->tray_id;
    auto agv_id = request->agv_id;

    if (place_tray_(tray_id, agv_id))
    {
        response->success = true;
        response->message = "Tray successfully placed on agv!";
        
    } else
    {
        response->success = false;
        response->message = "Unable to place tray on top of AGV";
    }
}

//=============================================//
void FloorRobot::place_part_on_tray_cb_(
    custom_msgs::srv::PlacingPart::Request::SharedPtr request,
    custom_msgs::srv::PlacingPart::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to move robot to tray");
    auto quadrant_id = request->quadrant_id;
    auto agv_id = request->agv_id;

    // Command Robot to place tray on agv (number obtained from kitting task)
    if(place_part_on_tray_(agv_id, quadrant_id)){
        response->success = true;
        response->message = "Part successfully on tray!";
        
    } else
    {
        response->success = false;
        response->message = "Unable to place part on the tray sitting on top of AGV";
    }
}

//=============================================//
bool FloorRobot::move_robot_to_tray_(int tray_id, const geometry_msgs::msg::Pose &tray_pose)
{
    double tray_rotation = Utils::get_yaw_from_pose_(tray_pose);

    std::vector<geometry_msgs::msg::Pose> waypoints;

    waypoints.push_back(Utils::build_pose(tray_pose.position.x, tray_pose.position.y,
                                          tray_pose.position.z + 0.2, set_robot_orientation_(tray_rotation)));
    waypoints.push_back(Utils::build_pose(tray_pose.position.x, tray_pose.position.y,
                                          tray_pose.position.z + pick_offset_, set_robot_orientation_(tray_rotation)));

    if (!move_through_waypoints_(waypoints, 0.3, 0.3))
    {
        RCLCPP_ERROR(get_logger(), "Unable to move robot above tray");
        return false;
    }

    // set_gripper_state_(true);

    wait_for_attach_completion_(5.0);

    
    if (floor_gripper_state_.attached){

        // Add tray to planning scene
        // TODO: This will generate all sorts of problems if "kit_tray_3" already exists in the planning scene
        // It can happen that you need to build 2 kits and both use tray id 3.
        // You should find a way to name each tray differently
        std::string tray_name = "kit_tray_" + std::to_string(tray_id);
        add_single_model_to_planning_scene_(tray_name, "kit_tray.stl", tray_pose);

        // Attach tray to robot in planning scene
        floor_robot_->attachObject(tray_name);

        // Move up slightly
        waypoints.clear();
        waypoints.push_back(Utils::build_pose(tray_pose.position.x, tray_pose.position.y,
                                              tray_pose.position.z + 0.2, set_robot_orientation_(tray_rotation)));

        if (!move_through_waypoints_(waypoints, 0.2, 0.2))
        {
            RCLCPP_ERROR(get_logger(), "Unable to move up");
            return false;
        }
        return true;
    }

    return false;
}

//=============================================//
void FloorRobot::move_tray_to_agv_srv_cb_(
    robot_commander_msgs::srv::MoveTrayToAGV::Request::SharedPtr request,
    robot_commander_msgs::srv::MoveTrayToAGV::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to move tray to agv");
    auto agv_number = request->agv_number;
    if (move_tray_to_agv(agv_number))
    {
        response->success = true;
        response->message = "Tray moved to AGV";
    }
    else
    {
        response->success = false;
        response->message = "Unable to move tray to AGV";
    }
}

//=============================================//
bool FloorRobot::move_tray_to_agv(int agv_number)
{
    std::vector<geometry_msgs::msg::Pose> waypoints;
    floor_robot_->setJointValueTarget("linear_actuator_joint", rail_positions_["agv" + std::to_string(agv_number)]);
    floor_robot_->setJointValueTarget("floor_shoulder_pan_joint", 0);

    if (!move_to_target_())
    {
        RCLCPP_ERROR(get_logger(), "Unable to move tray to AGV");
        return false;
    }

    auto agv_tray_pose = get_pose_in_world_frame_("agv" + std::to_string(agv_number) + "_tray");
    auto agv_rotation = Utils::get_yaw_from_pose_(agv_tray_pose);

    waypoints.clear();
    waypoints.push_back(Utils::build_pose(agv_tray_pose.position.x, agv_tray_pose.position.y,
                                          agv_tray_pose.position.z + 0.3, set_robot_orientation_(agv_rotation)));

    waypoints.push_back(Utils::build_pose(agv_tray_pose.position.x, agv_tray_pose.position.y,
                                          agv_tray_pose.position.z + kit_tray_thickness_ + drop_height_, set_robot_orientation_(agv_rotation)));

    if (!move_through_waypoints_(waypoints, 0.2, 0.1))
    {
        RCLCPP_ERROR(get_logger(), "Unable to move tray to AGV");
        return false;
    }
    return true;
}

//=============================================//
void FloorRobot::enter_tool_changer_srv_cb_(
    robot_commander_msgs::srv::EnterToolChanger::Request::SharedPtr request,
    robot_commander_msgs::srv::EnterToolChanger::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to enter tool changer");

    auto changing_station = request->changing_station;
    auto gripper_type = request->gripper_type;

    if (enter_tool_changer_(changing_station, gripper_type))
    {
        response->success = true;
        response->message = "Entered tool changer";
    }
    else
    {
        response->success = false;
        response->message = "Unable to enter tool changer";
    }
}

//=============================================//
bool FloorRobot::enter_tool_changer_(std::string changing_station, std::string gripper_type)
{

    usleep(10000);
    auto tc_pose = get_pose_in_world_frame_(changing_station + "_tool_changer_" + gripper_type + "_frame");

    RCLCPP_INFO_STREAM(get_logger(), "Tool changer pose: " << tc_pose.position.x << ", " << tc_pose.position.y << ", " << tc_pose.position.z);
    std::vector<geometry_msgs::msg::Pose> waypoints;
    waypoints.push_back(Utils::build_pose(tc_pose.position.x, tc_pose.position.y,
                                          tc_pose.position.z + 0.4, set_robot_orientation_(0.0)));

    waypoints.push_back(Utils::build_pose(tc_pose.position.x, tc_pose.position.y,
                                          tc_pose.position.z, set_robot_orientation_(0.0)));

    if (!move_through_waypoints_(waypoints, 0.3, 0.3))
        return false;

    return true;
}

//=============================================//
void FloorRobot::exit_tool_changer_srv_cb_(
    robot_commander_msgs::srv::ExitToolChanger::Request::SharedPtr request,
    robot_commander_msgs::srv::ExitToolChanger::Response::SharedPtr response)
{
    RCLCPP_INFO(get_logger(), "Received request to exit tool changer");

    auto changing_station = request->changing_station;
    auto gripper_type = request->gripper_type;

    if (exit_tool_changer_(changing_station, gripper_type))
    {
        response->success = true;
        response->message = "Exited tool changer";
    }
    else
    {
        response->success = false;
        response->message = "Unable to exit tool changer";
    }
}

//=============================================//
bool FloorRobot::exit_tool_changer_(std::string changing_station, std::string gripper_type)
{
    // Move gripper into tool changer
    auto tc_pose = get_pose_in_world_frame_(changing_station + "_tool_changer_" + gripper_type + "_frame");

    std::vector<geometry_msgs::msg::Pose> waypoints;

    waypoints.push_back(Utils::build_pose(tc_pose.position.x, tc_pose.position.y,
                                          tc_pose.position.z + 0.4, set_robot_orientation_(0.0)));

    if (!move_through_waypoints_(waypoints, 0.3, 0.3))
        return false;

    return true;
}

//=============================================//
bool FloorRobot::start_competition_()
{
    // Wait for competition state to be ready
    while (competition_state_ != ariac_msgs::msg::CompetitionState::READY)
    {
    }

    rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr client;

    std::string srv_name = "/ariac/start_competition";

    client = this->create_client<std_srvs::srv::Trigger>(srv_name);

    auto request = std::make_shared<std_srvs::srv::Trigger::Request>();

    auto result = client->async_send_request(request);
    result.wait();

    return result.get()->success;
}

//=============================================//
bool FloorRobot::end_competition_()
{
    rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr client;

    std::string srv_name = "/ariac/end_competition";

    client = this->create_client<std_srvs::srv::Trigger>(srv_name);

    auto request = std::make_shared<std_srvs::srv::Trigger::Request>();

    auto result = client->async_send_request(request);
    result.wait();

    return result.get()->success;
}

//=============================================//
bool FloorRobot::lock_tray_(int agv_num)
{
    rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr client;

    std::string srv_name = "/ariac/agv" + std::to_string(agv_num) + "_lock_tray";

    client = this->create_client<std_srvs::srv::Trigger>(srv_name);

    auto request = std::make_shared<std_srvs::srv::Trigger::Request>();

    auto result = client->async_send_request(request);
    result.wait();

    return result.get()->success;
}

//=============================================//
bool FloorRobot::move_agv_(int agv_num, int destination)
{
    rclcpp::Client<ariac_msgs::srv::MoveAGV>::SharedPtr client;

    std::string srv_name = "/ariac/move_agv" + std::to_string(agv_num);

    client = this->create_client<ariac_msgs::srv::MoveAGV>(srv_name);

    auto request = std::make_shared<ariac_msgs::srv::MoveAGV::Request>();
    request->location = destination;

    auto result = client->async_send_request(request);
    result.wait();

    return result.get()->success;
}

//=============================================//
void FloorRobot::agv1_status_cb(
    const ariac_msgs::msg::AGVStatus::ConstSharedPtr msg)
{
    agv_locations_[1] = msg->location;
}

//=============================================//
void FloorRobot::agv2_status_cb(
    const ariac_msgs::msg::AGVStatus::ConstSharedPtr msg)
{
    agv_locations_[2] = msg->location;
}

//=============================================//
void FloorRobot::agv3_status_cb(
    const ariac_msgs::msg::AGVStatus::ConstSharedPtr msg)
{
    agv_locations_[3] = msg->location;
}

//=============================================//
void FloorRobot::agv4_status_cb(
    const ariac_msgs::msg::AGVStatus::ConstSharedPtr msg)
{
    agv_locations_[4] = msg->location;
}

//=============================================//
void FloorRobot::orders_cb(
    const ariac_msgs::msg::Order::ConstSharedPtr msg)
{
    orders_.push_back(*msg);
}

//=============================================//
void FloorRobot::floor_robot_sub_cb(
    const std_msgs::msg::String::ConstSharedPtr msg)
{
    if (msg->data == "go_home")
    {
        if (go_home_())
        {
            RCLCPP_INFO(get_logger(), "Going home");
        }
        else
        {
            RCLCPP_ERROR(get_logger(), "Unable to go home");
        }
    }
}

//=============================================//
void FloorRobot::competition_state_cb(
    const ariac_msgs::msg::CompetitionState::ConstSharedPtr msg)
{
    competition_state_ = msg->competition_state;
}

//=============================================//
void FloorRobot::kts1_camera_cb(
    const ariac_msgs::msg::AdvancedLogicalCameraImage::ConstSharedPtr msg)
{
    if (!kts1_camera_received_data)
    {
        RCLCPP_INFO(get_logger(), "Received data from kts1 camera");
        kts1_camera_received_data = true;
    }

    kts1_trays_ = msg->tray_poses;
    kts1_camera_pose_ = msg->sensor_pose;
}

//=============================================//
void FloorRobot::kts2_camera_cb(
    const ariac_msgs::msg::AdvancedLogicalCameraImage::ConstSharedPtr msg)
{
    if (!kts2_camera_received_data)
    {
        RCLCPP_INFO(get_logger(), "Received data from kts2 camera");
        kts2_camera_received_data = true;
    }

    kts2_trays_ = msg->tray_poses;
    kts2_camera_pose_ = msg->sensor_pose;
}

//=============================================//
void FloorRobot::left_bins_camera_cb(
    const ariac_msgs::msg::AdvancedLogicalCameraImage::ConstSharedPtr msg)
{
    if (!left_bins_camera_received_data)
    {
        RCLCPP_INFO(get_logger(), "Received data from left bins camera");
        left_bins_camera_received_data = true;
    }

    left_bins_parts_ = msg->part_poses;
    left_bins_camera_pose_ = msg->sensor_pose;
}

//=============================================//
void FloorRobot::right_bins_camera_cb(
    const ariac_msgs::msg::AdvancedLogicalCameraImage::ConstSharedPtr msg)
{
    if (!right_bins_camera_received_data)
    {
        RCLCPP_INFO(get_logger(), "Received data from right bins camera");
        right_bins_camera_received_data = true;
    }

    right_bins_parts_ = msg->part_poses;
    right_bins_camera_pose_ = msg->sensor_pose;
}

//=============================================//
void FloorRobot::floor_gripper_state_cb(
    const ariac_msgs::msg::VacuumGripperState::ConstSharedPtr msg)
{
    floor_gripper_state_ = *msg;
    // RCLCPP_INFO_STREAM(get_logger(), "Floor gripper state: " << floor_gripper_state_.attached);
}

geometry_msgs::msg::Pose FloorRobot::get_pose_in_world_frame_(std::string frame_id)
{
    geometry_msgs::msg::TransformStamped t;
    geometry_msgs::msg::Pose pose;

    try
    {
        t = tf_buffer->lookupTransform("world", frame_id, tf2::TimePointZero);
    }
    catch (const tf2::TransformException &ex)
    {
        RCLCPP_ERROR(get_logger(), "Could not get transform");
    }

    pose.position.x = t.transform.translation.x;
    pose.position.y = t.transform.translation.y;
    pose.position.z = t.transform.translation.z;
    pose.orientation = t.transform.rotation;

    return pose;
}

//=============================================//
void FloorRobot::add_single_model_to_planning_scene_(
    std::string name, std::string mesh_file, geometry_msgs::msg::Pose model_pose)
{
    moveit_msgs::msg::CollisionObject collision;

    collision.id = name;
    collision.header.frame_id = "world";

    shape_msgs::msg::Mesh mesh;
    shapes::ShapeMsg mesh_msg;

    std::string package_share_directory = ament_index_cpp::get_package_share_directory("rwa67");
    std::stringstream path;
    path << "file://" << package_share_directory << "/meshes/" << mesh_file;
    std::string model_path = path.str();

    shapes::Mesh *m = shapes::createMeshFromResource(model_path);
    shapes::constructMsgFromShape(m, mesh_msg);

    mesh = boost::get<shape_msgs::msg::Mesh>(mesh_msg);

    collision.meshes.push_back(mesh);
    collision.mesh_poses.push_back(model_pose);

    collision.operation = collision.ADD;

    std::vector<moveit_msgs::msg::CollisionObject> collision_objects;
    collision_objects.push_back(collision);

    planning_scene_.addCollisionObjects(collision_objects);
}

//=============================================//
void FloorRobot::add_models_to_planning_scene_()
{
    // Add bins
    std::map<std::string, std::pair<double, double>> bin_positions = {
        {"bin1", std::pair<double, double>(-1.9, 3.375)},
        {"bin2", std::pair<double, double>(-1.9, 2.625)},
        {"bin3", std::pair<double, double>(-2.65, 2.625)},
        {"bin4", std::pair<double, double>(-2.65, 3.375)},
        {"bin5", std::pair<double, double>(-1.9, -3.375)},
        {"bin6", std::pair<double, double>(-1.9, -2.625)},
        {"bin7", std::pair<double, double>(-2.65, -2.625)},
        {"bin8", std::pair<double, double>(-2.65, -3.375)}};

    geometry_msgs::msg::Pose bin_pose;
    for (auto const &bin : bin_positions)
    {
        bin_pose.position.x = bin.second.first;
        bin_pose.position.y = bin.second.second;
        bin_pose.position.z = 0;
        bin_pose.orientation = Utils::get_quaternion_from_euler(0, 0, 3.14159);

        add_single_model_to_planning_scene_(bin.first, "bin.stl", bin_pose);
    }

    // Add assembly stations
    std::map<std::string, std::pair<double, double>> assembly_station_positions = {
        {"as1", std::pair<double, double>(-7.3, 3)},
        {"as2", std::pair<double, double>(-12.3, 3)},
        {"as3", std::pair<double, double>(-7.3, -3)},
        {"as4", std::pair<double, double>(-12.3, -3)},
    };

    geometry_msgs::msg::Pose assembly_station_pose;
    for (auto const &station : assembly_station_positions)
    {
        assembly_station_pose.position.x = station.second.first;
        assembly_station_pose.position.y = station.second.second;
        assembly_station_pose.position.z = 0;
        assembly_station_pose.orientation = Utils::get_quaternion_from_euler(0, 0, 0);

        add_single_model_to_planning_scene_(station.first, "assembly_station.stl", assembly_station_pose);
    }

    // Add assembly briefcases
    std::map<std::string, std::pair<double, double>> assembly_insert_positions = {
        {"as1_insert", std::pair<double, double>(-7.7, 3)},
        {"as2_insert", std::pair<double, double>(-12.7, 3)},
        {"as3_insert", std::pair<double, double>(-7.7, -3)},
        {"as4_insert", std::pair<double, double>(-12.7, -3)},
    };

    geometry_msgs::msg::Pose assembly_insert_pose;
    for (auto const &insert : assembly_insert_positions)
    {
        assembly_insert_pose.position.x = insert.second.first;
        assembly_insert_pose.position.y = insert.second.second;
        assembly_insert_pose.position.z = 1.011;
        assembly_insert_pose.orientation = Utils::get_quaternion_from_euler(0, 0, 0);

        add_single_model_to_planning_scene_(insert.first, "assembly_insert.stl", assembly_insert_pose);
    }

    geometry_msgs::msg::Pose conveyor_pose = geometry_msgs::msg::Pose();
    conveyor_pose.position.x = -0.6;
    conveyor_pose.position.y = 0;
    conveyor_pose.position.z = 0;
    conveyor_pose.orientation = Utils::get_quaternion_from_euler(0, 0, 0);

    add_single_model_to_planning_scene_("conveyor", "conveyor.stl", conveyor_pose);

    geometry_msgs::msg::Pose kts1_table_pose;
    kts1_table_pose.position.x = -1.3;
    kts1_table_pose.position.y = -5.84;
    kts1_table_pose.position.z = 0;
    kts1_table_pose.orientation = Utils::get_quaternion_from_euler(0, 0, 3.14159);

    add_single_model_to_planning_scene_("kts1_table", "kit_tray_table.stl", kts1_table_pose);

    geometry_msgs::msg::Pose kts2_table_pose;
    kts2_table_pose.position.x = -1.3;
    kts2_table_pose.position.y = 5.84;
    kts2_table_pose.position.z = 0;
    kts2_table_pose.orientation = Utils::get_quaternion_from_euler(0, 0, 0);

    add_single_model_to_planning_scene_("kts2_table", "kit_tray_table.stl", kts2_table_pose);
}

//=============================================//
geometry_msgs::msg::Quaternion FloorRobot::set_robot_orientation_(double rotation)
{
    tf2::Quaternion tf_q;
    tf_q.setRPY(0, 3.14159, rotation);

    geometry_msgs::msg::Quaternion q;

    q.x = tf_q.x();
    q.y = tf_q.y();
    q.z = tf_q.z();
    q.w = tf_q.w();

    return q;
}

//=============================================//
bool FloorRobot::move_to_target_()
{
    moveit::planning_interface::MoveGroupInterface::Plan plan;
    bool success = static_cast<bool>(floor_robot_->plan(plan));

    if (success)
    {
        return static_cast<bool>(floor_robot_->execute(plan));
    }
    else
    {
        RCLCPP_ERROR(get_logger(), "Unable to generate plan");
        return false;
    }
}

//=============================================//
bool FloorRobot::move_through_waypoints_(
    std::vector<geometry_msgs::msg::Pose> waypoints, double vsf, double asf)
{
    moveit_msgs::msg::RobotTrajectory trajectory;

    double path_fraction = floor_robot_->computeCartesianPath(waypoints, 0.01, 0.0, trajectory);

    if (path_fraction < 0.9)
    {
        RCLCPP_ERROR(get_logger(), "Unable to generate trajectory through waypoints");
        return false;
    }

    // Retime trajectory
    robot_trajectory::RobotTrajectory rt(floor_robot_->getCurrentState()->getRobotModel(), "floor_robot");
    rt.setRobotTrajectoryMsg(*floor_robot_->getCurrentState(), trajectory);
    totg_.computeTimeStamps(rt, vsf, asf);
    rt.getRobotTrajectoryMsg(trajectory);

    return static_cast<bool>(floor_robot_->execute(trajectory));
}

//=============================================//
void FloorRobot::wait_for_attach_completion_(double timeout, double dz)
{
    // Wait for part to be attached
    rclcpp::Time start = now();
    std::vector<geometry_msgs::msg::Pose> waypoints;
    geometry_msgs::msg::Pose starting_pose = floor_robot_->getCurrentPose().pose;

    while (!floor_gripper_state_.attached)
    {
        RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 1000, "Waiting for gripper attach");

        waypoints.clear();
        starting_pose.position.z -= dz;
        waypoints.push_back(starting_pose);

        move_through_waypoints_(waypoints, 0.1, 0.1);

        usleep(200);

        // if (floor_gripper_state_.attached)
        //     return;

         if (now() - start > rclcpp::Duration::from_seconds(timeout))
        {
            RCLCPP_ERROR(get_logger(), "Unable to pick up object");
            return;
        }
    }
}

//=============================================//
bool FloorRobot::go_home_()
{
    // Move floor robot to home joint state
    floor_robot_->setNamedTarget("home");
    return move_to_target_();
}

//=============================================//
bool FloorRobot::set_gripper_state_(bool enable)
{
    if (floor_gripper_state_.enabled == enable)
    {
        if (floor_gripper_state_.enabled)
            RCLCPP_INFO(get_logger(), "Already enabled");
        else
            RCLCPP_INFO(get_logger(), "Already disabled");

        return false;
    }

    // Call enable service
    auto request = std::make_shared<ariac_msgs::srv::VacuumGripperControl::Request>();
    request->enable = enable;

    auto result = floor_robot_gripper_enable_->async_send_request(request);
    result.wait();

    if (!result.get()->success)
    {
        RCLCPP_ERROR(get_logger(), "Error calling gripper enable service");
        return false;
    }

    return true;
}

//=============================================//
bool FloorRobot::change_gripper_(std::string changing_station, std::string gripper_type)
{
    // Move gripper into tool changer
    auto tc_pose = get_pose_in_world_frame_(changing_station + "_tool_changer_" + gripper_type + "_frame");

    std::vector<geometry_msgs::msg::Pose> waypoints;
    waypoints.push_back(Utils::build_pose(tc_pose.position.x, tc_pose.position.y,
                                          tc_pose.position.z + 0.4, set_robot_orientation_(0.0)));

    waypoints.push_back(Utils::build_pose(tc_pose.position.x, tc_pose.position.y,
                                          tc_pose.position.z, set_robot_orientation_(0.0)));

    if (!move_through_waypoints_(waypoints, 0.2, 0.1))
        return false;

    // Call service to change gripper
    auto request = std::make_shared<ariac_msgs::srv::ChangeGripper::Request>();

    if (gripper_type == "trays")
    {
        request->gripper_type = ariac_msgs::srv::ChangeGripper::Request::TRAY_GRIPPER;
    }
    else if (gripper_type == "parts")
    {
        request->gripper_type = ariac_msgs::srv::ChangeGripper::Request::PART_GRIPPER;
    }

    auto future = floor_robot_tool_changer_->async_send_request(request);

    future.wait();
    if (!future.get()->success)
    {
        RCLCPP_ERROR(get_logger(), "Error calling gripper change service");
        return false;
    }

    waypoints.clear();
    waypoints.push_back(Utils::build_pose(tc_pose.position.x, tc_pose.position.y,
                                          tc_pose.position.z + 0.4, set_robot_orientation_(0.0)));

    if (!move_through_waypoints_(waypoints, 0.2, 0.1))
        return false;

    return true;
}

//=============================================//
bool FloorRobot::place_tray_(int tray_id, int agv_num)
{

    // Track tray movement
    std::vector<geometry_msgs::msg::Pose> waypoints;

    geometry_msgs::msg::Pose tray_pose;

    double tray_rotation = Utils::get_yaw_from_pose_(tray_pose);

    std::string tray_name = "kit_tray_" + std::to_string(tray_id);

    // Move up slightly
    waypoints.clear();
    waypoints.push_back(Utils::build_pose(tray_pose.position.x, tray_pose.position.y,
                                          tray_pose.position.z + 0.2, set_robot_orientation_(tray_rotation)));
    move_through_waypoints_(waypoints, 0.3, 0.3);

    floor_robot_->setJointValueTarget("linear_actuator_joint", rail_positions_["agv" + std::to_string(agv_num)]);
    floor_robot_->setJointValueTarget("floor_shoulder_pan_joint", 0);

    move_to_target_();

    auto agv_tray_pose = get_pose_in_world_frame_("agv" + std::to_string(agv_num) + "_tray");
    auto agv_rotation = Utils::get_yaw_from_pose_(agv_tray_pose);

    waypoints.clear();
    waypoints.push_back(Utils::build_pose(agv_tray_pose.position.x, agv_tray_pose.position.y,
                                          agv_tray_pose.position.z + 0.3, set_robot_orientation_(agv_rotation)));

    waypoints.push_back(Utils::build_pose(agv_tray_pose.position.x, agv_tray_pose.position.y,
                                          agv_tray_pose.position.z + kit_tray_thickness_ + drop_height_, set_robot_orientation_(agv_rotation)));

    move_through_waypoints_(waypoints, 0.2, 0.1);

    set_gripper_state_(false);

    // object is detached in the planning scene
    floor_robot_->detachObject(tray_name);

    // publish to robot state
    // lock_tray_(agv_num);

    waypoints.clear();
    waypoints.push_back(Utils::build_pose(agv_tray_pose.position.x, agv_tray_pose.position.y,
                                          agv_tray_pose.position.z + 0.3, set_robot_orientation_(0)));

    move_through_waypoints_(waypoints, 0.2, 0.1);

    return true;
}

//=============================================//
bool FloorRobot::pick_bin_part_(ariac_msgs::msg::Part part_to_pick)
{
    RCLCPP_INFO_STREAM(get_logger(), "Attempting to pick a " << part_colors_[part_to_pick.color] << " " << part_types_[part_to_pick.type]);

    // Check if part is in one of the bins
    geometry_msgs::msg::Pose part_pose;
    bool found_part = false;
    std::string bin_side;

    // Check left bins
    for (auto part : left_bins_parts_)
    {
        if (part.part.type == part_to_pick.type && part.part.color == part_to_pick.color)
        {
            part_pose = Utils::multiply_poses(left_bins_camera_pose_, part.pose);
            found_part = true;
            bin_side = "left_bins";
            break;
        }
    }
    // Check right bins
    if (!found_part)
    {
        for (auto part : right_bins_parts_)
        {
            if (part.part.type == part_to_pick.type && part.part.color == part_to_pick.color)
            {
                part_pose = Utils::multiply_poses(right_bins_camera_pose_, part.pose);
                found_part = true;
                bin_side = "right_bins";
                break;
            }
        }
    }
    if (!found_part)
    {
        RCLCPP_ERROR(get_logger(), "Unable to locate part");
        return false;
    }

    double part_rotation = Utils::get_yaw_from_pose_(part_pose);

    // Change gripper at location closest to part
    if (floor_gripper_state_.type != "part_gripper")
    {
        std::string station;
        if (part_pose.position.y < 0)
        {
            station = "kts1";
        }
        else
        {
            station = "kts2";
        }

        // Move floor robot to the corresponding kit tray table
        if (station == "kts1")
        {
            floor_robot_->setJointValueTarget(floor_kts1_js_);
        }
        else
        {
            floor_robot_->setJointValueTarget(floor_kts2_js_);
        }
        move_to_target_();

        change_gripper_(station, "parts");
    }

    floor_robot_->setJointValueTarget("linear_actuator_joint", rail_positions_[bin_side]);
    floor_robot_->setJointValueTarget("floor_shoulder_pan_joint", 0);
    move_to_target_();

    std::vector<geometry_msgs::msg::Pose> waypoints;
    waypoints.push_back(Utils::build_pose(part_pose.position.x, part_pose.position.y,
                                          part_pose.position.z + 0.5, set_robot_orientation_(part_rotation)));

    waypoints.push_back(Utils::build_pose(part_pose.position.x, part_pose.position.y,
                                          part_pose.position.z + part_heights_[part_to_pick.type] + pick_offset_, set_robot_orientation_(part_rotation)));

    move_through_waypoints_(waypoints, 0.3, 0.3);

    set_gripper_state_(true);

    wait_for_attach_completion_(3.0);

    // Add part to planning scene
    std::string part_name = part_colors_[part_to_pick.color] + "_" + part_types_[part_to_pick.type];
    add_single_model_to_planning_scene_(part_name, part_types_[part_to_pick.type] + ".stl", part_pose);
    floor_robot_->attachObject(part_name);
    floor_robot_attached_part_ = part_to_pick;

    // Move up slightly
    waypoints.clear();
    waypoints.push_back(Utils::build_pose(part_pose.position.x, part_pose.position.y,
                                          part_pose.position.z + 0.3, set_robot_orientation_(0)));

    move_through_waypoints_(waypoints, 0.3, 0.3);

    return true;
}

//=============================================//
bool FloorRobot::place_part_on_tray_(int agv_num, int quadrant)
{
    if (!floor_gripper_state_.attached)
    {
        RCLCPP_ERROR(get_logger(), "No part attached");
        return false;
    }

    // Move to agv
    floor_robot_->setJointValueTarget("linear_actuator_joint", rail_positions_["agv" + std::to_string(agv_num)]);
    floor_robot_->setJointValueTarget("floor_shoulder_pan_joint", 0);
    move_to_target_();

    // Determine target pose for part based on agv_tray pose
    auto agv_tray_pose = get_pose_in_world_frame_("agv" + std::to_string(agv_num) + "_tray");

    auto part_drop_offset = Utils::build_pose(quad_offsets_[quadrant].first, quad_offsets_[quadrant].second, 0.0,
                                              geometry_msgs::msg::Quaternion());

    auto part_drop_pose = Utils::multiply_poses(agv_tray_pose, part_drop_offset);

    std::vector<geometry_msgs::msg::Pose> waypoints;

    waypoints.push_back(Utils::build_pose(part_drop_pose.position.x, part_drop_pose.position.y,
                                          part_drop_pose.position.z + 0.3, set_robot_orientation_(0)));

    waypoints.push_back(Utils::build_pose(part_drop_pose.position.x, part_drop_pose.position.y,
                                          part_drop_pose.position.z + part_heights_[floor_robot_attached_part_.type] + drop_height_ + 0.01,
                                          set_robot_orientation_(0)));

    move_through_waypoints_(waypoints, 0.3, 0.3);

    // Drop part in quadrant
    set_gripper_state_(false);

    std::string part_name = part_colors_[floor_robot_attached_part_.color] +
                            "_" + part_types_[floor_robot_attached_part_.type];
    floor_robot_->detachObject(part_name);

    waypoints.clear();
    waypoints.push_back(Utils::build_pose(part_drop_pose.position.x, part_drop_pose.position.y,
                                          part_drop_pose.position.z + 0.3,
                                          set_robot_orientation_(0)));

    move_through_waypoints_(waypoints, 0.2, 0.1);

    return true;
}

//=============================================//
void FloorRobot::remove_part_from_agv_srv_cb_(
        custom_msgs::srv::RemovePart::Request::SharedPtr req_, custom_msgs::srv::RemovePart::Response::SharedPtr res_)
{
    RCLCPP_INFO(get_logger(), "Received request to remove part from AGV");

    if (remove_part_from_tray_(req_->agv_id, req_->quadrant_id, req_->part_type, req_->part_color))
    {
        res_->success = true;
        res_->message = "Removed faulty part!";
    }
    else
    {
        res_->success = false;
        res_->message = "Unable to remove faulty part";
    }

}

//=============================================//
bool FloorRobot::remove_part_from_tray_(int agv_num, int quadrant, int part_type, int part_color)
{
    if (floor_gripper_state_.attached)
    {
        RCLCPP_ERROR(get_logger(), "Part still attached!");
        return false;
    }

    // // Move to agv
    // floor_robot_->setJointValueTarget("linear_actuator_joint", rail_positions_["agv" + std::to_string(agv_num)]);
    // floor_robot_->setJointValueTarget("floor_shoulder_pan_joint", 0);
    // move_to_target_();

    // Determine target pose for part based on agv_tray pose
    auto agv_tray_pose = get_pose_in_world_frame_("agv" + std::to_string(agv_num) + "_tray");

    auto part_drop_offset = Utils::build_pose(quad_offsets_[quadrant].first, quad_offsets_[quadrant].second, 0.0,
                                              geometry_msgs::msg::Quaternion());

    auto part_drop_pose = Utils::multiply_poses(agv_tray_pose, part_drop_offset);

    std::vector<geometry_msgs::msg::Pose> waypoints;

    waypoints.push_back(Utils::build_pose(part_drop_pose.position.x, part_drop_pose.position.y,
                                          part_drop_pose.position.z + 0.3, set_robot_orientation_(0)));

    waypoints.push_back(Utils::build_pose(part_drop_pose.position.x, part_drop_pose.position.y,
                                          part_drop_pose.position.z + part_heights_[part_type] + pick_offset_ + 0.01,
                                          set_robot_orientation_(0)));

    move_through_waypoints_(waypoints, 0.3, 0.3);

    wait_for_attach_completion_(10.0, 0.002);

    if (floor_gripper_state_.attached)
    {
        RCLCPP_INFO(this->get_logger(),"~~~~~~~~~~ Object attached! ~~~~~~~~~~");
        // Add part to planning scene
        std::string part_name = part_colors_[part_color] + "_" + part_types_[part_type];
        // add_single_model_to_planning_scene_(part_name, part_types_[part_type_] + ".stl", part_pose_);
        floor_robot_->attachObject(part_name);

        auto part_to_pick = ariac_msgs::msg::Part();
        part_to_pick.type = part_type;
        part_to_pick.color = part_color;
        floor_robot_attached_part_ = part_to_pick;
        RCLCPP_INFO_STREAM(get_logger(), "Adding to the planning scene");

        // raise gripper
        waypoints.clear();
        waypoints.push_back(Utils::build_pose(part_drop_pose.position.x, part_drop_pose.position.y,
                                          part_drop_pose.position.z + 0.3, set_robot_orientation_(0)));
        move_through_waypoints_(waypoints, 0.3, 0.3); 

        // move towards the central disposal bin
        // floor_robot_->setJointValueTarget(drop_disposal_js_);
        // move_to_target_();
        waypoints.push_back(Utils::build_pose(-2.2, 0.0,
                                          0.8, set_robot_orientation_(0)));
        waypoints.push_back(Utils::build_pose(-2.2, 0.0,
                                          0.5, set_robot_orientation_(0)));                                   
        move_through_waypoints_(waypoints, 0.3, 0.3); 

        // Drop part in quadrant
        set_gripper_state_(false);
        floor_robot_->detachObject(part_name);              

        return true;
    }
    else {
        RCLCPP_INFO(this->get_logger(),"~~~~~~~~~~ Object NOT attached! ~~~~~~~~~~");
        return false;
    }

}

//=============================================//
bool FloorRobot::complete_orders_()
{
    // Wait for first order to be published
    while (orders_.size() == 0)
    {
    }

    bool success;
    while (true)
    {
        if (competition_state_ == ariac_msgs::msg::CompetitionState::ENDED)
        {
            success = false;
            break;
        }

        if (orders_.size() == 0)
        {
            if (competition_state_ != ariac_msgs::msg::CompetitionState::ORDER_ANNOUNCEMENTS_DONE)
            {
                // wait for more orders
                RCLCPP_INFO(get_logger(), "Waiting for orders...");
                while (orders_.size() == 0)
                {
                }
            }
            else
            {
                RCLCPP_INFO(get_logger(), "Completed all orders");
                success = true;
                break;
            }
        }

        current_order_ = orders_.front();
        orders_.erase(orders_.begin());
        int kitting_agv_num = -1;

        if (current_order_.type == ariac_msgs::msg::Order::KITTING)
        {
            FloorRobot::complete_kitting_task_(current_order_.kitting_task);
            kitting_agv_num = current_order_.kitting_task.agv_number;
        }
        else
        {
            RCLCPP_INFO(get_logger(), "Ignoring non-kitting tasks.");
        }

        // loop until the AGV is at the warehouse
        auto agv_location = -1;
        while (agv_location != ariac_msgs::msg::AGVStatus::WAREHOUSE)
        {
            if (kitting_agv_num == 1)
                agv_location = agv_locations_[1];
            else if (kitting_agv_num == 2)
                agv_location = agv_locations_[2];
            else if (kitting_agv_num == 3)
                agv_location = agv_locations_[3];
            else if (kitting_agv_num == 4)
                agv_location = agv_locations_[4];
        }

        FloorRobot::submit_order_(current_order_.id);
    }
    return success;
}

//=============================================//
bool FloorRobot::submit_order_(std::string order_id)
{
    rclcpp::Client<ariac_msgs::srv::SubmitOrder>::SharedPtr client;
    std::string srv_name = "/ariac/submit_order";
    client = this->create_client<ariac_msgs::srv::SubmitOrder>(srv_name);
    auto request = std::make_shared<ariac_msgs::srv::SubmitOrder::Request>();
    request->order_id = order_id;

    auto result = client->async_send_request(request);
    result.wait();

    return result.get()->success;
}

//=============================================//
bool FloorRobot::complete_kitting_task_(ariac_msgs::msg::KittingTask task)
{

    go_home_();

    for (auto kit_part : task.parts)
    {
        pick_bin_part_(kit_part.part);
        place_part_on_tray_(task.agv_number, kit_part.quadrant);
    }

    // Check quality
    auto request = std::make_shared<ariac_msgs::srv::PerformQualityCheck::Request>();
    request->order_id = current_order_.id;
    auto result = quality_checker_->async_send_request(request);
    result.wait();

    if (!result.get()->all_passed)
    {
        RCLCPP_ERROR(get_logger(), "Issue with shipment");
    }

    // move agv to destination
    move_agv_(task.agv_number, task.destination);

    return true;
}