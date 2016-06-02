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

#ifndef POSE_ESTIMATION_H
#define POSE_ESTIMATION_H

#include <opencv2/core/core.hpp>
#include <vector>

#include "../../utilities/include/constants.h"

namespace fato {

void getPoseRansac(const std::vector<cv::Point3f>& model_points,
               const std::vector<cv::Point2f>& tracked_points,
               const cv::Mat& camera_model, int iterations, float distance,
               std::vector<int>& inliers, cv::Mat& rotation,
               cv::Mat& translation);

/**
 * @brief getPoseFromFlow: estimate the pose of the target from optical flow
 * @param prev_pts:  2d points in the previous frame
 * @param prev_depth: estimated depth in the previous frame
 * @param next_pts: 2d points position estimated by optical flow
 * @param nodal_x: nodal x of the camera
 * @param nodal_y: notal y of the camera
 * @param focal_x
 * @param focal_y
 * @param translation: estimated tx,ty,tz
 * @param rotation: estimated wx,wy,wz
 */
void getPoseFromFlow(const std::vector<cv::Point2f>& prev_pts,
                     const std::vector<float>& prev_depth,
                     const std::vector<cv::Point2f>& next_pts, float nodal_x,
                     float nodal_y, float focal_x, float focal_y,
                     std::vector<float>& translation, std::vector<float>& rotation);

void getPose2D(const std::vector<cv::Point2f*>& model_points,
               const std::vector<cv::Point2f*>& tracked_points, float& scale,
               float& angle);

//cv::Mat getPose3D(const std::vector<cv::Point3f*>& model_points,
//                  const std::vector<cv::Point3f*>& tracked_points,
//                  const std::vector<Status*>& points_status);

cv::Mat getRigidTransform(cv::Mat& a, cv::Mat& b);

cv::Mat getRigidTransform(cv::Mat& a, cv::Mat& b, std::vector<float>& cA,
                          std::vector<float>& cB);

void rotateBBox(const std::vector<cv::Point3f>& bBox, const cv::Mat& rotation,
                std::vector<cv::Point3f>& updatedBBox);

void rotatePoint(const cv::Point3f& point, const cv::Mat& rotation,
                 cv::Point3f& updatedPoint);

void rotatePoint(const cv::Vec3f& point, const cv::Mat& rotation,
                 cv::Vec3f& updatedPoint);

void rotatePoint(const cv::Vec3f& point, const cv::Mat& rotation,
                 cv::Point3f& updatedPoint);

void rotationVecToMat(const cv::Mat& vec, cv::Mat& mat);

}  // end namespace

#endif  // POSE_ESTIMATION_H
