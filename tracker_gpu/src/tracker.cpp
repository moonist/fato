/*****************************************************************************/
/*  Copyright (c) 2015, Alessandro Pieropan                                  */
/*  All rights reserved.                                                     */
/*                                                                           */
/*  Redistribution and use in source and binary forms, with or without       */
/*  modification, are permitted provided that the following conditions       */
/*  are met:                                                                 */
/*                                                                           */
/*  1. Redistributions of source code must retain the above copyright        */
/*  notice, this list of conditions and the following disclaimer.            */
/*                                                                           */
/*  2. Redistributions in binary form must reproduce the above copyright     */
/*  notice, this list of conditions and the following disclaimer in the      */
/*  documentation and/or other materials provided with the distribution.     */
/*                                                                           */
/*  3. Neither the name of the copyright holder nor the names of its         */
/*  contributors may be used to endorse or promote products derived from     */
/*  this software without specific prior written permission.                 */
/*                                                                           */
/*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS      */
/*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT        */
/*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR    */
/*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT     */
/*  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,   */
/*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT         */
/*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,    */
/*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY    */
/*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT      */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE    */
/*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.     */
/*****************************************************************************/


#include "Tracker.h"
#include "HelperFunctions.h"
#include "GpuTimer.h"

#include <utilities.h>
#include <DebugFunctions.h>
#include <DBScanClustering.h>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>
#define _USE_MATH_DEFINES
#include <math.h>
#include <limits>
#include <algorithm>
#include <utility>

//FIXME: namespace standardized to the rest of the package
namespace fato {

namespace gpu {

Tracker::Tracker()
    : num_features_(500),
      scale_factor_(1.1f),
      num_levels_(6),
      edge_threshold_(31),
      first_level_(0),
      wta_k_(2),
      score_type_(0),
      patch_size_(31),
      matcher_confidence_(0.8),
      matcher_ratio_(0.8) {}

bool Tracker::isPointValid(const int& id) {
  // auto tmp_id = m_upd_to_init_ids[id];

  // std::cout << tmp_id << " "  << id << std::endl;

  return m_pointsStatus[id] == FatoStatus::TRACK;
}

void Tracker::init(const cv::Mat& rgb, const cv::Point2d& fst,
                   const cv::Point2d& scd) {
  auto mask = getMask(rgb.rows, rgb.cols, fst, scd);
  init(rgb, mask);
}

void Tracker::init(const Mat& rgb, const Mat& mask) {
  GpuMat d_gray;
  GpuMat d_rgb;
  Mat gray;

  m_width = rgb.cols;
  m_height = rgb.rows;

  d_rgb.upload(rgb);
  cvtColor(d_rgb, d_gray, CV_BGR2GRAY);
  cvtColor(rgb, gray, CV_BGR2GRAY);

  // BRISK featuresDetector;
  // featuresDetector.detect(gray, m_initKeypoints);
  // featuresDetector.compute(gray, m_initKeypoints, m_initDescriptors);

  // int nLevels = 6;
  // int edgeThreshold = 31;
  // int firstLevel = 0;
  // int WTA_K = 2;
  // int scoreType = 0;
  // int patchSize = 31;

  cv::gpu::ORB_GPU m_orbDetector(num_features_, scale_factor_, num_levels_,
                                 edge_threshold_, first_level_, wta_k_,
                                 score_type_, patch_size_);

  /*cv::gpu::ORB_GPU m_orbDetector(num_features_, scale_factor_, num_levels_,
                                 edge_threshold_, first_level_, wta_k_,
                                 score_type_, patch_size_);*/

  m_orbDetector(d_gray, GpuMat(), dm_initKeypoints, dm_initDescriptors);
  m_orbDetector.downloadKeyPoints(dm_initKeypoints, m_initKeypoints);
  dm_initDescriptors.download(m_initDescriptors);

  for (int i = 0; i < m_initKeypoints.size(); i++) {
    Point2f pt = m_initKeypoints[i].pt;

    m_points.push_back(pt);
  }

  // m_init_to_upd_ids.resize(m_initKeypoints.size(), -1);

  for (int i = 0; i < m_initKeypoints.size(); i++) {
    Point2f& pt = m_points[i];

    if (mask.at<uchar>(pt) == 255) {
      m_pointsStatus.push_back(FatoStatus::INIT);
    } else {
      m_pointsStatus.push_back(FatoStatus::BACKGROUND);
    }
    m_updatedPoints.push_back(m_points[i]);
    m_votes.push_back(Point2f(0, 0));
    m_upd_to_init_ids.push_back(i);
    m_init_to_upd_ids.push_back(i);
  }

  m_points_status_debug = m_pointsStatus;
  m_original_model_size = m_points.size();
  /****************************************************************************/
  /*                          INIT CENTROID                                   */
  /****************************************************************************/
  // cout << "Init centroid" << endl;
  m_initCentroid = initCentroid(m_updatedPoints);
  m_updatedCentroid = m_initCentroid;
  // cout << m_updatedCentroid << endl;
  /****************************************************************************/
  /*                          INIT RELATIVE DISTANCES                         */
  /****************************************************************************/
  // cout << "Init relative distance" << endl;
  initRelativeDistance(m_updatedPoints, m_initCentroid, m_relativeDistances);
  /****************************************************************************/
  /*                          INIT BOUNDING BOX                               */
  /****************************************************************************/
  // cout << "Init bounding box" << endl;
  initBoundingBox(mask, m_initCentroid, m_boundingBox, m_boundingBoxRelative,
                  m_boundingBoxUpdated);
  /****************************************************************************/
  /*                          INIT LEARNING                                   */
  /****************************************************************************/
  m_learned_poses.insert(make_pair<float, float>(0, 0));
  m_angle = 0;
  m_scale = 1;
  m_scale_old = m_scale;
  m_angle_old = m_angle;
  m_centroid_old = m_initCentroid;
  /****************************************************************************/
  /*                          INIT THREADS                                    */
  /****************************************************************************/
  m_isRunning = true;
  m_trackerStatus = async(launch::async, &Tracker::runTracker, this);
  m_detectorStatus = async(launch::async, &Tracker::runDetector, this);
  /****************************************************************************/
  /*                          LOADING IMGS ON GPU                             */
  /****************************************************************************/
  // cout << "load images" << endl;
  rgb.copyTo(m_init_rgb_img);
  dm_prev.upload(rgb);
  cv::gpu::cvtColor(dm_prev, dm_prevGray, CV_BGR2GRAY);
}

void Tracker::setFeatureExtractionParameters(int num_features,
                                             float scale_factor, int num_levels,
                                             int edge_threshold,
                                             int first_level, int patch_size) {
  num_features_ = num_features;
  scale_factor_ = scale_factor;
  num_levels_ = num_levels;
  edge_threshold_ = edge_threshold;
  first_level_ = first_level;
  patch_size_ = patch_size;
}

void Tracker::setMatcerParameters(float confidence, float second_ratio) {
  matcher_confidence_ = confidence;
  matcher_ratio_ = second_ratio;
}

Point2f Tracker::initCentroid(const vector<Point2f>& points) {
  Point2f centroid(0, 0);
  int validPoints = 0;

  for (size_t i = 0; i < points.size(); i++) {
    if (m_pointsStatus[i] == FatoStatus::INIT) {
      centroid += points[i];
      validPoints++;
    }
  }

  centroid.x = centroid.x / static_cast<float>(validPoints);
  centroid.y = centroid.y / static_cast<float>(validPoints);

  return centroid;
}

void Tracker::extractFeatures(const GpuMat& gray,
                              std::vector<KeyPoint>& keypoints,
                              Mat& descriptors) {
  cv::gpu::ORB_GPU orbDetector;
  orbDetector(gray, GpuMat(), dm_initKeypoints, dm_initDescriptors);
  orbDetector.downloadKeyPoints(dm_initKeypoints, m_initKeypoints);
  dm_initDescriptors.download(m_initDescriptors);
}

void Tracker::extractFeatures(const Mat& gray, std::vector<KeyPoint>& keypoints,
                              Mat& descriptors) {
  m_featuresDetector.detect(gray, keypoints);
  m_featuresDetector.compute(gray, keypoints, descriptors);
}

void Tracker::initRelativeDistance(const vector<Point2f>& points,
                                   const Point2f& centroid,
                                   vector<Point2f>& relDistances) {
  relDistances.reserve(points.size());

  for (size_t i = 0; i < points.size(); i++) {
    relDistances.push_back(points[i] - centroid);
  }
}

void Tracker::initBoundingBox(const Mat& mask, const Point2f& centroid,
                              vector<Point2f>& initBox,
                              vector<Point2f>& relativeBox,
                              vector<Point2f>& updBox) {
  int minX = numeric_limits<int>::max();
  int minY = minX;
  int maxX = 0;
  int maxY = 0;

  for (size_t j = 0; j < mask.rows; j++) {
    for (size_t i = 0; i < mask.cols; i++) {
      if (mask.at<uchar>(j, i) != 0) {
        minX = minX < i ? minX : i;
        maxX = maxX > i ? maxX : i;
        minY = minY < j ? minY : j;
        maxY = maxY > j ? maxY : j;
      }
    }
  }

  initBox.push_back(Point2f(minX, minY));
  initBox.push_back(Point2f(maxX, minY));
  initBox.push_back(Point2f(maxX, maxY));
  initBox.push_back(Point2f(minX, maxY));

  for (size_t i = 0; i < initBox.size(); i++) {
    relativeBox.push_back(initBox[i] - centroid);
  }

  updBox = initBox;
}

void Tracker::getOpticalFlow(const GpuMat& d_prev, const GpuMat& d_next,
                             vector<Point2f>& points, vector<int>& ids,
                             vector<FatoStatus>& status) {
  // uploading points to the gpu
  cv::gpu::GpuMat d_nextStatus, d_prevStatus, d_prevCalcPts, d_prevPts,
      d_nextPts;
  upload(points, d_prevPts);

  m_dPyrLK.sparse(d_prev, d_next, d_prevPts, d_nextPts, d_nextStatus);
  m_dPyrLK.sparse(d_next, d_prev, d_nextPts, d_prevCalcPts, d_prevStatus);

  vector<Point2f> prevPoints(d_prevPts.cols);
  vector<Point2f> prevCalcPoints(d_prevCalcPts.cols);
  vector<Point2f> nextPoints(d_nextPts.cols);

  download(d_prevPts, prevPoints);
  download(d_prevCalcPts, prevCalcPoints);
  download(d_nextPts, nextPoints);

  vector<uchar> ofStatus(d_prevStatus.cols);
  download(d_prevStatus, ofStatus);

  m_flow_counter = 0;
  for (int i = 0; i < nextPoints.size(); ++i) {
    float error = pinot_tracker::getDistance(prevCalcPoints[i], prevPoints[i]);

    FatoStatus& s = status[ids[i]];

    if (ofStatus[i] == 1 && error < 20) {
      // const int& id = ids[i];
      auto id = i;

      if (s == FatoStatus::MATCH) {
        status[id] = FatoStatus::TRACK;
        m_flow_counter++;
      } else if (s == FatoStatus::LOST)
        status[id] = FatoStatus::LOST;
      else if (s == FatoStatus::NOCLUSTER) {
        status[id] = FatoStatus::LOST;
      } else if (s == FatoStatus::TRACK || s == FatoStatus::INIT) {
        status[id] = FatoStatus::TRACK;
        m_flow_counter++;
      }

      points[i] = nextPoints[i];
    } else {
      // status[ids[i]] = Status::LOST;
      if (status[i] != FatoStatus::BACKGROUND) status[i] = FatoStatus::LOST;
      // TODO: remove pointer and ids if lost, it will still be in the detector
      // list
    }
  }
}

float Tracker::getMedianRotation(const vector<Point2f>& initPoints,
                                 const vector<Point2f>& updPoints,
                                 const vector<int>& ids) {
  vector<double> angles;
  angles.reserve(updPoints.size());

  for (size_t i = 0; i < updPoints.size(); i++) {
    for (size_t j = 0; j < updPoints.size(); j++) {
      if (isPointValid(ids[i]) && isPointValid(ids[j])) {
        Point2f a = updPoints[i] - updPoints[j];
        Point2f b = initPoints[ids[i]] - initPoints[ids[j]];

        double val = atan2(a.y, a.x) - atan2(b.y, b.x);

        if (abs(val) > M_PI) {
          int sign = (val < 0) ? -1 : 1;
          val = val - sign * 2 * M_PI;
        }
        angles.push_back(val);
      }
    }
  }

  sort(angles.begin(), angles.end());

  double median;
  size_t size = angles.size();

  if (size == 0) return 0;

  if (size % 2 == 0) {
    median = (angles[size / 2 - 1] + angles[size / 2]) / 2;
  } else {
    median = angles[size / 2];
  }

  return static_cast<float>(median);
}

float Tracker::getMedianScale(const vector<Point2f>& initPoints,
                              const vector<Point2f>& updPoints,
                              const vector<int>& ids) {
  vector<float> scales;

  for (size_t i = 0; i < updPoints.size(); ++i) {
    for (size_t j = 0; j < updPoints.size(); j++) {
      if (isPointValid(ids[i]) && isPointValid(ids[j])) {
        float nextDistance =
            pinot_tracker::getDistance(updPoints[i], updPoints[j]);
        float currDistance =
            pinot_tracker::getDistance(initPoints[ids[i]], initPoints[ids[j]]);

        if (currDistance != 0 && i != j) {
          scales.push_back(nextDistance / currDistance);
        }
      }
    }
  }

  sort(scales.begin(), scales.end());

  float median;
  size_t size = scales.size();

  if (size == 0) {
    return 1;
  }

  if (size % 2 == 0) {
    median = (scales[size / 2 - 1] + scales[size / 2]) / 2;
  } else {
    median = scales[size / 2];
  }

  return median;
}

void Tracker::voteForCentroid(const vector<Point2f>& relativeDistances,
                              const vector<Point2f>& updPoints,
                              const float& angle, const float& scale,
                              vector<Point2f>& votes) {
  Mat2f rotMat(2, 2);

  rotMat.at<float>(0, 0) = cosf(angle);
  rotMat.at<float>(0, 1) = sinf(angle);
  rotMat.at<float>(1, 0) = -sinf(angle);
  rotMat.at<float>(1, 1) = cosf(angle);

  // int voteCount = 0;

  // cout << "VOTES: P " << updPoints.size() << " V: " << votes.size() << endl;

  for (size_t i = 0; i < updPoints.size(); i++) {
    if (isPointValid(i)) {
      const Point2f& a = updPoints[i];
      const Point2f& rm = relativeDistances[i];

      votes[i] = a - scale * pinot_tracker::mult(rotMat, rm);
    }
  }
}

void Tracker::clusterVotes(vector<Point2f>& centroidVotes,
                           vector<bool>& isClustered) {
  DBScanClustering<Point2f*> clusterer;

  vector<Point2f*> votes;
  vector<unsigned int> indices;

  for (unsigned int i = 0; i < centroidVotes.size(); i++) {
    if (isPointValid(i)) {
      votes.push_back(&centroidVotes[i]);
      indices.push_back(i);
    }
  }

  clusterer.clusterPoints(&votes, 15, 5, [](Point2f* a, Point2f* b) {
    return sqrt(pow(a->x - b->x, 2) + pow(a->y - b->y, 2));
  });

  auto res = clusterer.getClusters();
  int maxVal = 0;
  int maxId = -1;

  for (size_t i = 0; i < res.size(); i++) {
    if (res[i].size() > maxVal) {
      maxVal = res[i].size();
      maxId = i;
    }
  }

  isClustered.resize(centroidVotes.size(), false);

  int clusterVoteSize = 0;

  if (maxId > -1) {
    for (size_t i = 0; i < res[maxId].size(); i++) {
      unsigned int& id = indices[res[maxId][i]];
      isClustered[id] = true;
    }

    clusterVoteSize = res[maxId].size();
  }
}

void Tracker::updateCentroid(const float& angle, const float& scale,
                             const vector<Point2f>& votes,
                             const vector<bool>& isClustered,
                             Point2f& updCentroid) {
  updCentroid.x = 0;
  updCentroid.y = 0;

  int validKp = 0;

  for (int i = 0; i < votes.size(); ++i) {
    if (isPointValid(i) && isClustered[i]) {
      updCentroid += votes[i];
      validKp++;
    }
  }

  if (validKp == 0) {
    m_is_object_lost = true;
  } else {
    m_is_object_lost = false;
    updCentroid.x = updCentroid.x / static_cast<float>(validKp);
    updCentroid.y = updCentroid.y / static_cast<float>(validKp);
  }
}

void Tracker::updatePointsStatus(const vector<bool>& isClustered,
                                 vector<Point2f>& points,
                                 vector<Point2f>& votes,
                                 vector<Point2f>& relDistances,
                                 vector<int>& ids,
                                 vector<FatoStatus>& pointsStatus) {
  vector<int> toBeRemoved;

  for (int i = 0; i < isClustered.size(); ++i) {
    if (!isClustered[i]) {
      toBeRemoved.push_back(i);
      pointsStatus[ids[i]] = FatoStatus::LOST;
    }
  }

  int back = points.size() - 1;

  for (int i = 0; i < toBeRemoved.size(); ++i) {
    const int dest = back - i;
    const int src = toBeRemoved[i];
    std::swap(points[src], points[dest]);
    std::swap(votes[src], votes[dest]);
    std::swap(ids[src], ids[dest]);
    std::swap(relDistances[src], relDistances[dest]);
  }

  int reducedSize = points.size() - toBeRemoved.size();
  points.resize(reducedSize);
  votes.resize(reducedSize);
  ids.resize(reducedSize);
  relDistances.resize(reducedSize);
}

void Tracker::labelNotClusteredPts(const vector<bool>& isClustered,
                                   vector<Point2f>& points,
                                   vector<Point2f>& votes,
                                   vector<Point2f>& relDistances,
                                   vector<int>& ids,
                                   vector<FatoStatus>& pointsStatus) {
  for (int i = 0; i < isClustered.size(); ++i) {
    if (!isClustered[i] && m_pointsStatus[ids[i]] != FatoStatus::BACKGROUND) {
      pointsStatus[ids[i]] = FatoStatus::LOST;
    }
    // else if (isClustered[i] && pointsStatus[ids[i]] == Status::NOCLUSTER)
    //  pointsStatus[ids[i]] = Status::TRACK;
  }
}

void Tracker::discardNotClustered(std::vector<Point2f>& upd_points,
                                  std::vector<Point2f>& init_pts,
                                  cv::Point2f& upd_centroid,
                                  cv::Point2f& init_centroid,
                                  std::vector<int>& ids,
                                  std::vector<FatoStatus>& pointsStatus) {
  for (auto i = 0; i < upd_points.size(); ++i) {
    auto id = ids[i];

    if (pointsStatus[id] == FatoStatus::NOCLUSTER) {
      float init_dist = pinot_tracker::getDistance(init_pts[id], init_centroid);
      float upd_dist =
          pinot_tracker::getDistance(upd_points[i], upd_centroid) * m_scale;

      float ratio = min(init_dist, upd_dist) / max(init_dist, upd_dist);

      if (ratio < 0.85) pointsStatus[id] = FatoStatus::LOST;
    }
  }
}

void Tracker::removeLostPoints(const std::vector<bool>& isClustered,
                               std::vector<Point2f>& points,
                               std::vector<Point2f>& votes,
                               std::vector<Point2f>& relDistances,
                               std::vector<int>& ids,
                               std::vector<FatoStatus>& pointsStatus) {
  vector<int> toBeRemoved;

  for (int i = 0; i < points.size(); ++i) {
    const int& id = ids[i];
    if (pointsStatus[i] == FatoStatus::LOST) {
      toBeRemoved.push_back(i);
    }
  }

  int back = points.size() - 1;

  for (int i = 0; i < toBeRemoved.size(); ++i) {
    const int dest = back - i;
    const int src = toBeRemoved[i];
    std::swap(points[src], points[dest]);
    std::swap(votes[src], votes[dest]);
    std::swap(ids[src], ids[dest]);
    std::swap(relDistances[src], relDistances[dest]);
  }

  int reducedSize = points.size() - toBeRemoved.size();
  points.resize(reducedSize);
  votes.resize(reducedSize);
  ids.resize(reducedSize);
  relDistances.resize(reducedSize);
}

void Tracker::updateBoundingBox(const float& angle, const float& scale,
                                const vector<Point2f>& boundingBoxRelative,
                                const Point2f& updCentroid,
                                vector<Point2f>& updBox) {
  Mat2f rotMat(2, 2);

  rotMat.at<float>(0, 0) = cosf(angle);
  rotMat.at<float>(0, 1) = sinf(angle);
  rotMat.at<float>(1, 0) = -sinf(angle);
  rotMat.at<float>(1, 1) = cosf(angle);

  for (int i = 0; i < updBox.size(); ++i) {
    updBox[i] = updCentroid + scale * pinot_tracker::mult(rotMat, boundingBoxRelative[i]);
  }
}

void Tracker::computeNext(const Mat& next) {
  // cout << "Computing next! " << endl;

  m_nextRgb = next;
  m_completed = 0;
  m_trackerDone = false;
  m_matcherDone = false;

  m_learn_new_pose = false;

  m_trackerCondition.notify_one();
  m_detectorCondition.notify_one();

  std::chrono::microseconds sleepTime(1);

  while (!m_trackerDone || !m_matcherDone) {
    // do nothing
    std::this_thread::sleep_for(sleepTime);
  }
  /****************************************************************************/
  /*                LEARNING PROCEDURE                                        */
  /****************************************************************************/
  // m_learn_new_pose = evaluatePose(m_angle, m_scale);
  /*if (m_learn_new_pose && !m_is_object_lost) {
    learnPose(m_boundingBoxUpdated, dm_prevGray, m_points, m_updatedPoints,
              m_pointsStatus, m_init_to_upd_ids);
  }*/
  /****************************************************************************/
  /*                STORE COMPUTATIONS                                        */
  /****************************************************************************/
  m_scale_old = m_scale;
  m_angle_old = m_angle;
  m_centroid_old = m_updatedCentroid;
}

void Tracker::taskFinished() {
  m_isRunning = false;
  m_trackerCondition.notify_one();
  m_detectorCondition.notify_one();
}

int Tracker::runTracker() {
  // cv::gpu::setDevice(0);

  GpuTimer timer;
  m_trackerFrameCount = 0;
  m_trackerTime = 0;

  cout << "Tracker initialized!" << endl;

  while (m_isRunning) {
    unique_lock<mutex> lock(m_trackerMutex);
    m_trackerCondition.wait(lock);
    if (!m_isRunning) return 0;
    if (m_nextRgb.empty()) return 0;
    /*********************************************************************************/
    timer.Start();
    /*********************************************************************************/
    Mat rgb = m_nextRgb;
    // cout << "before tracker" << endl;
    trackNext(rgb);
    // cout << "after tracker" << endl;
    m_completed++;
    m_trackerDone = true;
    /*********************************************************************************/
    timer.Stop();
    m_trackerTime += timer.Elapsed();
    m_trackerFrameCount++;
    /*********************************************************************************/
  }

  cout << "Tracker stopped!" << endl;
  return 0;
}

int Tracker::runDetector() {
  cout << "Before device call!" << endl;

  // cudaThreadExit();
  // cv::gpu::setDevice(1);

  cout << "Before timer call!" << endl;

  GpuTimer timer;
  m_detectorFrameCount = 0;
  m_detectorTime = 0;

  cout << "Detector initialized!" << endl;

  while (m_isRunning) {
    unique_lock<mutex> lock(m_detectorMutex);
    m_detectorCondition.wait(lock);
    if (!m_isRunning) return 0;
    if (m_nextRgb.empty()) return 0;
    /*********************************************************************************/
    timer.Start();
    /*********************************************************************************/
    Mat rgb = m_nextRgb;
    // cout << "before detector" << endl;
    detectNext(rgb);
    // cout << "after detector" << endl;
    m_matcherDone = true;
    m_completed++;
    /*********************************************************************************/
    timer.Stop();
    m_detectorTime += timer.Elapsed();
    m_detectorFrameCount++;
    /*********************************************************************************/
  }

  cout << "Matcher stopped!" << endl;
  return 0;
}

void Tracker::trackNext(Mat next) {
  /*************************************************************************************/
  /*                       LOADING IMAGES */
  /*************************************************************************************/
  GpuMat d_next, d_nextGray;
  d_next.upload(next);
  cvtColor(d_next, d_nextGray, CV_BGR2GRAY);
  /*************************************************************************************/
  /*                       TRACKING */
  /*************************************************************************************/
  // cout << "Optical flow " << endl;
  getOpticalFlow(dm_prevGray, d_nextGray, m_updatedPoints, m_upd_to_init_ids,
                 m_pointsStatus);
  /*************************************************************************************/
  /*                             ANGLE /
  /*************************************************************************************/
  float angle = getMedianRotation(m_points, m_updatedPoints, m_upd_to_init_ids);
  m_angle = angle;
  // angle = 0;
  // m_angle = 0.0f;
  // cout << "Angle " << angle << endl;
  /*************************************************************************************/
  /*                             SCALE */
  /*************************************************************************************/
  // cout << "Scale " << endl;
  float scale = getMedianScale(m_points, m_updatedPoints, m_upd_to_init_ids);
  m_scale = scale;
  // cout << "Scale " << scale << endl;
  /*************************************************************************************/
  /*                             VOTING */
  /*************************************************************************************/
  // cout << "Vote " << endl;
  voteForCentroid(m_relativeDistances, m_updatedPoints, angle, scale, m_votes);
  /*************************************************************************************/
  /*                             CLUSTERING */
  /*************************************************************************************/
  // cout << "Cluster " << endl;
  vector<bool> isClustered;
  clusterVotes(m_votes, isClustered);
  /*************************************************************************************/
  /*                             UPDATING LIST OF POINTS */
  /*************************************************************************************/
  // updatePointsStatus(isClustered, m_updatedPoints, m_votes,
  // m_relativeDistances,
  //                   m_pointsIds, m_pointsStatus);
  // TODO: tracker works better without removing points here, WHY?!?
  labelNotClusteredPts(isClustered, m_updatedPoints, m_votes,
                       m_relativeDistances, m_upd_to_init_ids, m_pointsStatus);

  // discardNotClustered(m_updatedPoints, m_points, m_updatedCentroid,
  //                    m_initCentroid, m_upd_to_init_ids, m_pointsStatus);

  // removeLostPoints(isClustered, m_updatedPoints, m_votes,
  //                 m_relativeDistances, m_pointsIds, m_pointsStatus);
  /*************************************************************************************/
  /*                             UPDATING CENTROID */
  /*************************************************************************************/
  // cout << "Upd centroid " << endl;
  updateCentroid(angle, scale, m_votes, isClustered, m_updatedCentroid);
  /*************************************************************************************/
  /*                             UPDATING BOX */
  /*************************************************************************************/
  // cout << "Upd box " << endl;
  updateBoundingBox(angle, scale, m_boundingBoxRelative, m_updatedCentroid,
                    m_boundingBoxUpdated);
  /*************************************************************************************/
  /*                       SWAP MEMORY */
  /*************************************************************************************/
  dm_prev.swap(d_next);
  dm_prevGray.swap(d_nextGray);
}

void Tracker::detectNext(Mat next) {
  /*************************************************************************************/
  /*                       LOADING IMAGES */
  /*************************************************************************************/

  GpuMat d_next, d_nextGray, d_keypoints, d_descriptors;
  // GpuTimer tm;
  // tm.Start();
  d_next.upload(next);
  cvtColor(d_next, d_nextGray, CV_BGR2GRAY);
  // tm.Stop();
  // cout << "Upload and grayscale in " << tm.Elapsed() << " ms " << endl;
  /*************************************************************************************/
  /*                       FEATURE EXTRACTION */
  /*************************************************************************************/
  // ORB_GPU d_orbExtractor;
  Mat descriptors, gray;
  vector<KeyPoint> keypoints;

  // int nLevels = 6;
  // int edgeThreshold = 31;
  // int firstLevel = 0;
  // int WTA_K = 2;
  // int scoreType = 0;
  // int patchSize = 31;

  cv::gpu::ORB_GPU orbDetector(num_features_, scale_factor_, num_levels_,
                               edge_threshold_, first_level_, wta_k_,
                               score_type_, patch_size_);

  orbDetector(d_nextGray, GpuMat(), d_keypoints, d_descriptors);
  d_descriptors.download(descriptors);
  orbDetector.downloadKeyPoints(d_keypoints, keypoints);
  // alternative with brisk

  // cvtColor(next, gray, CV_BGR2GRAY);
  // m_featuresDetector.detect(gray, keypoints);
  // m_featuresDetector.compute(gray, keypoints, descriptors);

  /*************************************************************************************/
  /*                       FEATURE MATCHING */
  /*************************************************************************************/
  vector<vector<DMatch>> matches;
  m_customMatcher.match32(descriptors, m_initDescriptors, 2, matches);
  // BFMatcher_GPU d_matcher(NORM_HAMMING);
  // d_matcher.match(d_descriptors, dm_initDescriptors, matches);

  /*************************************************************************************/
  /*                      SYNC WITH TRACKER */
  /*************************************************************************************/
  std::chrono::microseconds sleepTime(10);
  while (!m_trackerDone) {
    // sleep doing anything
    this_thread::sleep_for(sleepTime);
  }
  /*************************************************************************************/
  /*                       ADD FEATURES TO TRACKER */
  /*************************************************************************************/
  m_match_counter = 0;

  for (size_t i = 0; i < matches.size(); i++) {
    const int& queryId = matches[i][0].queryIdx;
    const int& trainId = matches[i][0].trainIdx;

    if (queryId < 0 && queryId >= keypoints.size()) continue;
    if (trainId < 0 && trainId >= m_initKeypoints.size()) continue;

    float confidence = 1 - (matches[i][0].distance / 256.0);
    float ratio = matches[i][0].distance / matches[i][1].distance;

    FatoStatus& s = m_pointsStatus[trainId];

    if (confidence < 0.80f) continue;

    if (ratio > 0.8f) continue;

    if (s == FatoStatus::BACKGROUND) continue;

    // if (s == Status::TRACK) continue;

    m_pointsStatus[trainId] = FatoStatus::MATCH;
    // m_updatedPoints.push_back(keypoints[queryId].pt);
    // m_votes.push_back(Point2f(0, 0));
    // m_pointsIds.push_back(trainId);
    // m_relativeDistances.push_back(m_initKeypoints[trainId].pt -
    // m_initCentroid);
    // auto id = m_init_to_upd_ids[trainId];
    // cout << "TID " << trainId << " id " << id << endl;
    m_updatedPoints[trainId] = keypoints[queryId].pt;
    // m_votes[trainId] = Point2f(0, 0);
  }

  /*Mat debug_img;
  debug_img.create(Size(m_width*2, m_height), next.type());
  next.copyTo(debug_img(Rect(0, 0, m_width, m_height)));
  m_init_rgb_img.copyTo(debug_img(Rect(m_width, 0, m_width, m_height)));

  for(auto i = 0; i < m_pointsStatus.size(); i++)
  {
    if(m_pointsStatus[i] == Status::MATCH)
    {
      circle(debug_img, m_updatedPoints[i], 3, Scalar(0,255,0), 1);
      Point2f p = m_points[i];
      p.x += next.cols;
      circle(debug_img, p, 3, Scalar(0,255,0), 1);
      line(debug_img, m_updatedPoints[i], p, Scalar(0,255,0), 1);
    }
  }

  imshow("Matcher debug", debug_img);*/
  // waitKey(0);
}

bool Tracker::evaluatePose(const float& angle, const float& scale) {
  // discretize angle
  float angle_change = pinot_tracker::roundDownToNearest(angle, 0.30);
  // discretize scale
  float scale_change = pinot_tracker::roundDownToNearest(scale - 1, 0.30);

  /****************************************************************************/
  /**                adding to history                                        */
  /****************************************************************************/
  if (m_angle_history.size() > 15) m_angle_history.pop_front();
  m_angle_history.push_back(angle);

  if (m_scale_history.size() > 15) m_scale_history.pop_front();
  m_scale_history.push_back(scale);

  if (m_center_history.size() > 15) m_center_history.pop_front();
  m_center_history.push_back(m_updatedCentroid);
  /****************************************************************************/
  /**                Computing variation                                      */
  /****************************************************************************/

  auto it_scale = m_scale_history.begin();
  auto prev_val_scale = *it_scale;
  auto max_variation_scale = 0.0f;
  while (it_scale != m_scale_history.end()) {
    max_variation_scale =
        std::max(max_variation_scale, abs(prev_val_scale - *it_scale));
    prev_val_scale = *it_scale;
    ++it_scale;
  }

  auto it_angle = m_angle_history.begin();
  auto prev_val_angle = *it_angle;
  auto max_variation_angle = 0.0f;
  while (it_angle != m_angle_history.end()) {
    max_variation_angle =
        std::max(max_variation_angle, abs(prev_val_angle - *it_angle));
    prev_val_angle = *it_angle;
    ++it_angle;
  }

  auto it_mov = m_center_history.begin();
  auto prev_val_mov = *it_mov;
  auto max_variation_mov = 0.0f;
  while (it_mov != m_center_history.end()) {
    max_variation_mov = std::max(
        max_variation_mov, pinot_tracker::getDistance(prev_val_mov, *it_mov));
    prev_val_mov = *it_mov;
    ++it_mov;
  }

  // check if the new pose in already stored in the tracker
  pair<float, float> p(angle_change, scale_change);

  if (max_variation_angle > 0.9 || max_variation_mov > 3.0 ||
      max_variation_scale > 0.5)
    return false;

  auto it_pose = m_learned_poses.find(p);

  bool is_new_pose = false;
  if (it_pose == m_learned_poses.end()) {
    m_learned_poses.insert(p);
    is_new_pose = true;
  }

  if (is_new_pose) {
    cout << fixed << setprecision(2) << "Angle change " << angle_change
         << " scale change " << scale_change << " angle delta "
         << max_variation_angle << " scale delta " << max_variation_scale
         << " mov_delta " << max_variation_mov << endl;

    // waitKey(0);
  }

  return is_new_pose;
}

void Tracker::learnPose(const std::vector<cv::Point2f>& bbox,
                        const GpuMat& d_gray, std::vector<Point2f>& init_pts,
                        std::vector<Point2f>& upd_pts,
                        std::vector<FatoStatus>& pts_status,
                        std::vector<int>& pts_id) {
  // extract features
  cv::gpu::GpuMat d_keypoints;
  cv::gpu::GpuMat d_descriptors;
  ORB_GPU orbDetector;
  orbDetector(d_gray, GpuMat(), d_keypoints, d_descriptors);
  // find feature points that belongs to the model
  std::vector<KeyPoint> keypoints;
  std::vector<Point2f> points_to_add;
  Mat descriptors;
  orbDetector.downloadKeyPoints(d_keypoints, keypoints);
  d_descriptors.download(descriptors);
  // draw mask of the object
  Mat1b mask(m_height, m_width, static_cast<uchar>(0));
  pinot_tracker::drawTriangleMask(bbox[0], bbox[1], bbox[2], mask);
  pinot_tracker::drawTriangleMask(bbox[0], bbox[2], bbox[3], mask);

  // Mat debug_img;
  // m_nextRgb.copyTo(debug_img);

  Mat descriptors_to_add;
  for (int i = 0; i < keypoints.size(); i++) {
    Point2f& pt = keypoints[i].pt;
    if (mask.at<uchar>(pt) == 255) {
      points_to_add.push_back(pt);
      descriptors_to_add.push_back(descriptors.row(i));
      // circle(debug_img, pt, 3, Scalar(255, 0, 0), 1);
    }
  }

  // imshow("Learn Debug", debug_img);
  // waitKey(0);*/

  auto pts_size = m_points.size();

  vector<Point2f> projected_pts;
  projectPointsToModel(m_initCentroid, m_updatedCentroid, m_angle, m_scale,
                       points_to_add, projected_pts);

  Mat init_debug;
  m_init_rgb_img.copyTo(init_debug);

  std::cout << m_points.size() << std::endl;

  for (auto i = 0; i < points_to_add.size(); i++) {
    if (m_points.size() > 2000) break;

    auto id = i + pts_size;
    auto pt = points_to_add[i];
    auto pt_prj = projected_pts[i];
    m_pointsStatus.push_back(FatoStatus::MATCH);
    m_updatedPoints.push_back(pt);
    m_votes.push_back(Point2f(0, 0));
    m_upd_to_init_ids.push_back(id);
    m_points.push_back(pt_prj);
    m_relativeDistances.push_back(pt_prj - m_initCentroid);
    m_initDescriptors.push_back(descriptors_to_add.row(i));
    // TODO: remove this after experiments are done
    m_points_status_debug.push_back(FatoStatus::LEARN);
    circle(init_debug, pt_prj, 3, Scalar(255, 0, 0), 1);
  }

  cout << "Size of points " << m_points.size() << endl;
  cout << "Size of descriptors " << m_initDescriptors.rows << endl;
  cout << "Size of points extracted " << points_to_add.size() << endl;

  // imshow("Learn Debug", init_debug);
  // waitKey(0);
}

void Tracker::projectPointsToModel(const Point2f& model_centroid,
                                   const Point2f& upd_centroid,
                                   const float angle, const float scale,
                                   const std::vector<Point2f>& pts,
                                   std::vector<Point2f>& proj_pts) {
  Mat2f rotMat(2, 2);
  rotMat.at<float>(0, 0) = cosf(angle);
  rotMat.at<float>(0, 1) = -sinf(angle);
  rotMat.at<float>(1, 0) = sinf(angle);
  rotMat.at<float>(1, 1) = cosf(angle);

  for (auto i = 0; i < pts.size(); i++) {
    Point2f rm = pts[i] - upd_centroid;
    if (scale != 0) {
      rm.x = rm.x / scale;
      rm.y = rm.y / scale;
    }
    rm = pinot_tracker::mult(rotMat, rm);

    proj_pts.push_back(model_centroid + rm);
  }
}

}  // end namespace gpu

}  // end namespace pinot
