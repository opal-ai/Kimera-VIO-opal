/* ----------------------------------------------------------------------------
 * Copyright 2017, Massachusetts Institute of Technology,
 * Cambridge, MA 02139
 * All Rights Reserved
 * Authors: Luca Carlone, et al. (see THANKS for the full author list)
 * See LICENSE for the license information
 * -------------------------------------------------------------------------- */

/**
 * @file   Pipeline.h
 * @brief  Implements VIO pipeline workflow.
 * @author Antoni Rosinol
 */

#pragma once

#include <stddef.h>
#include <atomic>
#include <cstdlib>  // for srand()
#include <memory>
#include <thread>
#include <utility>  // for make_pair
#include <vector>

#include "kimera-vio/backend/VioBackEnd-definitions.h"
#include "kimera-vio/backend/VioBackEndModule.h"
#include "kimera-vio/datasource/DataSource-definitions.h"
#include "kimera-vio/frontend/FeatureSelector.h"
#include "kimera-vio/frontend/StereoImuSyncPacket.h"
#include "kimera-vio/frontend/StereoVisionFrontEnd.h"
#include "kimera-vio/initial/InitializationBackEnd-definitions.h"
#include "kimera-vio/loopclosure/LoopClosureDetector.h"
#include "kimera-vio/mesh/MesherModule.h"
#include "kimera-vio/pipeline/Pipeline-definitions.h"
#include "kimera-vio/utils/ThreadsafeQueue.h"
#include "kimera-vio/visualizer/Visualizer3DModule.h"

namespace VIO {

class Pipeline {
 private:
  KIMERA_POINTER_TYPEDEFS(Pipeline);
  KIMERA_DELETE_COPY_CONSTRUCTORS(Pipeline);
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  // Typedefs
  typedef std::function<void(const SpinOutputPacket&)>
      KeyframeRateOutputCallback;

 public:
  explicit Pipeline(const PipelineParams& params);

  virtual ~Pipeline();

  // Main spin, runs the pipeline.
  void spin(StereoImuSyncPacket::UniquePtr stereo_imu_sync_packet);

  // Run an endless loop until shutdown to visualize.
  bool spinViz();

  // Spin the pipeline only once.
  void spinOnce(StereoImuSyncPacket::UniquePtr stereo_imu_sync_packet);

  // A parallel pipeline should always be able to run sequentially...
  void spinSequential();

  // Shutdown the pipeline once all data has been consumed.
  void shutdownWhenFinished();

  // Shutdown processing pipeline: stops and joins threads, stops queues.
  // And closes logfiles.
  void shutdown();

  // Resumes all queues
  void resume();

  // Callback to output the VIO backend results at keyframe rate.
  // This callback also allows to
  inline void registerKeyFrameRateOutputCallback(
      KeyframeRateOutputCallback callback) {
    keyframe_rate_output_callback_ = callback;
  }

  // Callback to output the LoopClosureDetector's loop-closure/PGO results.
  inline void registerLcdPgoOutputCallback(
      const LcdModule::OutputCallback& callback) {
    if (lcd_module_) {
      lcd_module_->registerCallback(callback);
    } else {
      LOG(ERROR) << "Attempt to register LCD/PGO callback, but no "
                 << "LoopClosureDetector member is active in pipeline.";
    }
  }

 private:
  // Initialize random seed for repeatability (only on the same machine).
  // TODO Still does not make RANSAC REPEATABLE across different machines.
  inline void setDeterministicPipeline() const { srand(0); }

  // Initialize pipeline with desired option (flag).
  bool initialize(const StereoImuSyncPacket& stereo_imu_sync_packet);

  // Check if necessary to re-initialize pipeline.
  void checkReInitialize(const StereoImuSyncPacket& stereo_imu_sync_packet);

  // Initialize pipeline from ground truth pose.
  bool initializeFromGroundTruth(
      const StereoImuSyncPacket& stereo_imu_sync_packet,
      const VioNavState& initial_ground_truth_state);

  // Initialize pipeline from IMU readings only:
  //  - Guesses initial state assuming zero velocity.
  //  - Guesses IMU bias assuming steady upright vehicle.
  bool initializeFromIMU(const StereoImuSyncPacket& stereo_imu_sync_packet);

  // Initialize pipeline from online gravity alignment.
  bool initializeOnline(const StereoImuSyncPacket& stereo_imu_sync_packet);

  // Displaying must be done in the main thread.
  void spinDisplayOnce(const VisualizerOutput::Ptr& viz_output) const;

  StatusStereoMeasurements featureSelect(
      const VioFrontEndParams& tracker_params,
      const Timestamp& timestamp_k,
      const Timestamp& timestamp_lkf,
      const gtsam::Pose3& W_Pose_Blkf,
      double* feature_selection_time,
      std::shared_ptr<StereoFrame>& stereoFrame_km1,
      const StatusStereoMeasurements& smart_stereo_meas,
      int cur_kf_id,
      int save_image_selector,
      const gtsam::Matrix& curr_state_cov,
      const Frame& left_frame);

  // Launch different threads with processes.
  void launchThreads();

  // Launch frontend thread with process.
  void launchFrontendThread();

  // Launch remaining threads with processes.
  void launchRemainingThreads();

  // Shutdown processes and queues.
  void stopThreads();

  // Join threads to do a clean shutdown.
  void joinThreads();

  // Callbacks.
  KeyframeRateOutputCallback keyframe_rate_output_callback_;

  // Init Vio parameter
  VioBackEndParams::ConstPtr backend_params_;
  VioFrontEndParams frontend_params_;
  ImuParams imu_params_;

  //! Definition of sensor rig used
  StereoCamera::UniquePtr stereo_camera_;

  // TODO this should go to another class to avoid not having copy-ctor...
  // Frontend.
  StereoVisionFrontEndModule::UniquePtr vio_frontend_module_;
  std::unique_ptr<FeatureSelector> feature_selector_;

  // Stereo vision frontend payloads.
  StereoVisionFrontEndModule::InputQueue stereo_frontend_input_queue_;

  // Online initialization frontend queue.
  ThreadsafeQueue<InitializationInputPayload::UniquePtr>
      initialization_frontend_output_queue_;

  // Create VIO: class that implements estimation back-end.
  VioBackEndModule::UniquePtr vio_backend_module_;

  // Thread-safe queue for the backend.
  VioBackEndModule::InputQueue backend_input_queue_;

  // Create class to build mesh.
  MesherModule::UniquePtr mesher_module_;

  // Create class to detect loop closures.
  LcdModule::UniquePtr lcd_module_;

  // Visualization process.
  VisualizerModule::UniquePtr visualizer_module_;

  // Shutdown switch to stop pipeline, threads, and queues.
  std::atomic_bool shutdown_ = {false};
  std::atomic_bool is_initialized_ = {false};
  std::atomic_bool is_launched_ = {false};
  int init_frame_id_;

  // Threads.
  std::unique_ptr<std::thread> frontend_thread_ = {nullptr};
  std::unique_ptr<std::thread> backend_thread_ = {nullptr};
  std::unique_ptr<std::thread> mesher_thread_ = {nullptr};
  std::unique_ptr<std::thread> lcd_thread_ = {nullptr};
  std::unique_ptr<std::thread> visualizer_thread_ = {nullptr};

  BackendType backend_type_;
  bool parallel_run_;
};

}  // namespace VIO
