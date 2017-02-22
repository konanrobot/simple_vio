//
// Created by lancelot on 1/17/17.
//

#include <limits>

#include "InitialImpl.h"
#include "DataStructure/cv/cvFrame.h"
#include "DataStructure/imu/imuFactor.h"
#include "util/util.h"


InitialImpl::InitialImpl() {
    imu_ =  std::make_shared<IMU>();
}


bool InitialImpl::init(std::vector<std::shared_ptr<viFrame>> &VecFrames,
                       std::vector<std::shared_ptr<imuFactor>>& VecImuFactor,
                       std::shared_ptr<ImuParameters> &imuParam, int n_iter) {
    assert(VecImuFactor.size() + 1 == VecFrames.size());

    const double convergeErr = 0.03 * 0.03;

    size_t size = VecFrames.size();

    if(size < 3) {
        printf("frames are too few\n");
        return false;
    }

    if(size < 6) {
        printf("warning: the information got from initializing steps may not be ensured!\n");
    }

    //! esitmate gbias;
    Eigen::Vector3d gbias(0.0, 0.0, 0.0);
    double Err;
    Eigen::Vector3d b;
    Eigen::Matrix3d H;
    double errOld;
    bool converge = false;

    for(int iter = 0; iter < n_iter; ++iter) {
        if(iter == 0)
            errOld = std::numeric_limits<double>::max();
        else
            errOld = Err;

        Err = 0.0;
        H.setZero();
        b.setZero();
        for (int i = 0; i < size - 1; ++i) {
            const IMUMeasure::Transformation &deltaPose = VecImuFactor[i]->deltaPose;
            const imuFactor::FacJBias_t &facJac = VecImuFactor[i]->getJBias();
            Eigen::Vector3d err = Sophus::SO3d::log(
                    (deltaPose.so3() * Sophus::SO3d::exp(facJac.block<3, 3>(0, 0) * gbias)).inverse()
                    * VecFrames[i]->getPose().so3() * VecFrames[i + 1]->getPose().so3().inverse());
            Err += err.transpose() * err;
            Eigen::Matrix3d Jac = -leftJacobian(err).inverse() * facJac.block<3, 3>(0, 0);
            b += Jac.transpose() * err;
            H += Jac.transpose() * Jac;
        }

        if(Err > errOld)
            return false;

        if(std::abs(Err - errOld) <= convergeErr) {
            for(size_t i = 0; i < size; ++i) {
                VecFrames[i]->spbs.block<3, 1>(3, 0) = gbias;
            }
            converge = true;
            break;
        }

        Eigen::Vector3d delta_bg = H.ldlt().solve(-b);
        gbias += delta_bg;
    }

    if(false == converge)
        return false;

    //! estimate scale and gravity; refine bias_a, scale, gravity
    double scale = 1.0;
    Eigen::Vector3d cP_B = dynamic_cast<VIOPinholeCamera*>(VecFrames[0]->cvframe->getCam().get())->getT_BS().inverse().translation();
    Eigen::Vector3d g_w = imuParam->g * Eigen::Vector3d(0, 0, -1);
    Eigen::MatrixXd A(3 * (size - 2), 4);
    Eigen::MatrixXd B(3 * (size - 2), 1);
    Eigen::MatrixXd C(3 * (size - 2), 6);
    Eigen::MatrixXd D(3 * (size - 2), 1);
    Eigen::Vector3d p1 = VecFrames[0]->cvframe->getPose().translation();
    Eigen::Vector3d p2;
    Eigen::Vector3d p3;
    Sophus::SO3d R1_wc = VecFrames[0]->cvframe->getPose().so3().inverse();
    Sophus::SO3d R2_wc = VecFrames[1]->cvframe->getPose().so3().inverse();
    Sophus::SO3d R3_wc = VecFrames[2]->cvframe->getPose().so3().inverse();
    Sophus::SO3d R1_wb = VecFrames[0]->getPose().so3().inverse();
    Sophus::SO3d R2_wb = VecFrames[1]->getPose().so3().inverse();
    Sophus::SO3d R3_wb = VecFrames[2]->getPose().so3().inverse();
    double dt12 = (VecFrames[1]->getTimeStamp() - VecFrames[0]->getTimeStamp()).toSec();
    p2 = scale * p1 + VecFrames[0]->spbs.block<3, 1>(0, 0) * dt12 + 0.5 * g_w * dt12 * dt12 +
         R1_wb * VecImuFactor[0]->getPoseFac().translation() + (R1_wc.matrix() - R2_wc.matrix()) * cP_B;

    double dt23 = (VecFrames[2]->getTimeStamp() - VecFrames[1]->getTimeStamp()).toSec();
    p3 = scale * p2 + VecFrames[1]->spbs.block<3, 1>(0, 0) * dt23 + 0.5 * g_w * dt23 * dt23 +
            R2_wb * VecImuFactor[1]->getPoseFac().translation() + (R2_wc.matrix() - R3_wc.matrix()) * cP_B;

    for(int i = 1; i < size - 1; ++i) {
        if(1 != i) {
            p1 = p2;
            p2 = p3;
            R1_wb = R2_wb;
            R2_wb = R3_wb;
            R3_wb = VecFrames[i + 1]->getPose().so3().inverse();
            R1_wc = R2_wc;
            R2_wc = R3_wc;
            R3_wc = VecFrames[i + 1]->cvframe->getPose().so3().inverse();
            dt12 = dt23;
            dt23 = (VecFrames[i + 1]->getTimeStamp() - VecFrames[i]->getTimeStamp()).toSec();
            p3 = scale * p2 + VecFrames[i]->spbs.block<3, 1>(0, 0) * dt23 + 0.5 * g_w * dt23 * dt23 +
                    R2_wb * VecImuFactor[1]->getPoseFac().translation() + (R2_wc.matrix() - R3_wc.matrix()) * cP_B;
        }

        C.block<3, 1>(3 * i -3, 0) = A.block<3, 1>(3 * i - 3, 0) = (p2 - p1) * dt23 - (p3 - p2) * dt12;
        A.block<3, 3>(3 * i - 3, 1) = 0.5 * Eigen::Matrix3d::Identity() * (dt12 * dt12 * dt23 - dt23 * dt23 * dt12);
        B.block<3, 1>(3 * i - 3, 0) = (R2_wc.matrix() - R1_wc.matrix()) * cP_B * dt23
                                      - (R3_wc.matrix() - R2_wc.matrix()) * cP_B * dt12
                                      + R2_wb * VecImuFactor[i]->getPoseFac().translation() * dt12
                                      - R1_wb * VecImuFactor[i - 1]->getSpeedFac() * dt12 * dt23
                                      - R1_wb * VecImuFactor[i - 1]->getPoseFac().translation() * dt23;

        C.block<3, 3>(3 * i - 3, 3) = R2_wb.matrix() * VecImuFactor[i]->getJBias().block<3, 3>(9, 0) * dt12
                                      + R1_wb.matrix() * VecImuFactor[i]->getJBias().block<3, 3>(3, 0) * dt12 * dt23
                                      - R1_wb.matrix() * VecImuFactor[i - 1]->getJBias().block<3, 3>(9, 0) * dt23;

        C(3 * i - 3, 1) = 0.5 * imuParam->g * dt12 * dt12 * dt23 + dt23 * dt23 * dt12;
        C(3 * i - 3, 2) = 0.5 * imuParam->g /*dt_ij^2 */ ;
        D.block<3, 1>(3 * i - 3, 0) = (R2_wc.matrix() - R1_wc.matrix()) * cP_B * dt23
                                      - (R3_wc.matrix() - R2_wc.matrix()) * cP_B * dt12
                                      + R2_wb * VecImuFactor[i]->getPoseFac().translation() * dt12
                                      + R1_wb * VecImuFactor[i - 1]->getSpeedFac() * dt12 * dt23
                                      - R1_wb * VecImuFactor[i - 1]->getPoseFac().translation() * dt23;
    }

    Eigen::Vector4d s_g = A.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(B);
    Eigen::Vector3d g_I = Eigen::Vector3d(0, 0, -1);
    Eigen::Vector3d g_what = s_g.block<3, 1>(1, 0);
    g_what.normalize();
    Eigen::Vector3d vhat = crossMx(g_I) * g_what;
    vhat.normalize();
    double theta = std::atan2((crossMx(g_I) * g_what).norm(), g_I.dot(g_what));

    Sophus::SO3d R_WI = Sophus::SO3d::exp(theta * vhat);
    for(int i = 1; i < size - 1; ++i) {
        C.block<3, 2>(3 * i - 3, 1) = (C(3 * i - 3, 1) * R_WI.matrix() * Sophus::SO3d::hat(Eigen::Vector3d(0, 0, -1))).block<3, 2>(0, 1);
        D.block<3, 1>(3 * i - 3, 0) += R_WI * Eigen::Vector3d(0, 0, -1) * C(3 * i - 3, 2);
    }

    Eigen::Matrix<double, 6, 1> s_dxy_ba = C.jacobiSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(D);
    g_what = (R_WI * Sophus::SO3d::exp(Eigen::Vector3d(s_dxy_ba(1, 0), s_dxy_ba(2, 0), 0))) * Eigen::Vector3d(0, 0, 1) * imuParam->g;


    //! estimate velocity

}
