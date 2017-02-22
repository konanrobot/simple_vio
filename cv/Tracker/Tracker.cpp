//
// Created by lancelot on 2/20/17.
//


#include <memory>
#include "Tracker.h"
#include "DataStructure/cv/cvFrame.h"
#include "DataStructure/viFrame.h"
#include "DataStructure/cv/Feature.h"
#include "DataStructure/cv/Point.h"

namespace direct_tracker {

    typedef Eigen::Matrix<double, 1, 6> Jac_t;
    bool compute_EdgeJac(std::shared_ptr<viFrame> &viframe_i,
                         std::shared_ptr<viFrame> &viframe_j,
                         std::shared_ptr<Feature> &ft,
                         const Sophus::SE3d T_SB,
                         const Sophus::SE3d& Ti,
                         Sophus::SE3d& T_ji,
                         Jac_t &jac,
                         double &w,
                         double &err) {
        std::shared_ptr<Point>& p = ft->point;
        Eigen::Vector2d& dir = ft->grad;
        Eigen::Vector2d grad;
        Eigen::Vector3d P = T_ji * Ti * p->pos_;
        const viFrame::cam_t & cam = viframe_j->getCam();
        Eigen::Vector3d Pj = T_SB * P;
        if(Pj(2) < 0.001)
            return false;

        double u = cam->fx() * Pj(0) / Pj(2) + cam->cx();
        if(u < 0 || u >= viframe_j->getCVFrame()->getWidth())
            return false;

        double v = cam->fy() * Pj(1) / Pj(2) + cam->cy();

        if(v < 0 || v >= viframe_j->getCVFrame()->getWidth())
            return false;

        for(int i = 0; i < ft->level; ++i) {
            u /= 2.0;
            v /= 2.0;
        }

        if(!viframe_j->getCVFrame()->getGrad(u, v, grad, ft->level))
            return false;

        w = 1.0 / viframe_j->getCVFrame()->getGradNorm(u, v, ft->level);
        if(w < 0.001 || std::isinf(w))
            return false;

        Eigen::Vector2d px = viframe_i->getCam()->world2cam(p->pos_);
        int ui = std::floor(px[0]);
        int vi = std::floor(px[1]);
        int uj = std::floor(u);
        int vj = std::floor(v);

        //! bilinear interpolation


        err = viframe_i->getCVFrame()->getIntensity(px(0), px(1), ft->level)
                - viframe_j->getCVFrame()->getIntensity(u, v, ft->level);

        //! if err is to large, should I compute this point?

        double Ix = dir(1) * dir(0);
        double Iy = Ix * grad(0) + dir(1) * dir(1) * grad(1);
        Ix *= grad(1);
        Ix += dir(0) * dir(0) * grad(0);
        jac(0, 0) =  cam->fx() /  Pj(2);
        jac(0, 1) = cam->fy() / Pj(2);
        jac(0, 2) = -jac(0, 0) * Pj(0) / Pj(2);
        jac(0, 3) = -jac(0, 1) * Pj(1) / Pj(2);
        jac(0, 0) *= Ix;
        jac(0, 1) *= Iy;
        jac(0, 2) = jac(0, 2) * Ix + jac(0, 3) * Iy;
        Eigen::Matrix<double, 3, 6> JacR;
        JacR.block<3, 3>(0, 0) = T_SB.so3().matrix();
        JacR.block<3, 3>(0, 3) = - Sophus::SO3d::hat(P) * JacR.block<3, 3>(0, 0);

        return true;
    }

    bool compute_PointJac(std::shared_ptr<viFrame> &viframe_i,
                          std::shared_ptr<viFrame> &viframe_j,
                          std::shared_ptr<Feature> &ft,
                          const Sophus::SE3d T_SB,
                          const Sophus::SE3d& Ti,
                          Sophus::SE3d& T_ji,
                          Jac_t &jac,
                          double &w,
                          double &err){
    }


    Tracker::Tracker() {}

    bool Tracker::Tracking(std::shared_ptr<viFrame> &viframe_i, std::shared_ptr<viFrame> &viframe_j,
                           Sophus::SE3d &T_ji, int n_iter) {
        bool converge = false;
        int cnt = 0;
        int iter = 0;
        Eigen::Matrix<double, 6, 6> H;
        Eigen::Matrix<double, 6, 1> b;
        const std::shared_ptr<cvMeasure::features_t>& fts = viframe_i->getCVFrame()->getMeasure().fts_;
        Jac_t jac;
        int cnt_min = int(fts->size()) / 3;
        Sophus::SE3d T_SB = viframe_j->getT_BS().inverse();
        double w = 0;
        double e = 0;
        Eigen::Matrix<double, 6, 1> xi;
        double chi = std::numeric_limits<double>::max();
        double chi_new = 0;

        for(; iter < n_iter; iter++) {
            H.setZero();
            b.setZero();
            cnt = 0;
            jac.setZero();
            for(auto ft : (*fts)) {
                if(ft->type == Feature::EDGELET) {
                    if(!direct_tracker::compute_EdgeJac(viframe_i, viframe_j, ft, T_SB, viframe_i->getPose(), T_ji, jac, w, e))
                        continue;
                    cnt++;
                    H += jac.transpose() * jac * w;
                    b += jac.transpose() * e * w;

                } else {
                   if(!direct_tracker::compute_PointJac(viframe_i, viframe_j, ft, T_SB, viframe_i->getPose(), T_ji, jac, w, e))
                       continue;
                    cnt++;
                    H += jac.transpose() * jac * w;
                    b += jac.transpose() * e * w;
                }
            }

            if(cnt <= cnt_min && cnt <= 12)
                return false;

            xi = H.ldlt().solve(-b);
            chi_new = b.transpose() * b;
            if(chi_new < chi) {
                T_ji = Sophus::SE3d::exp(xi) * T_ji;
                if(chi - chi_new < 1.0e-2) {
                    converge = true;
                    break;
                }
            }
            else {
                if(chi_new - chi < 1.0e-2) {
                    converge = true;
                    break;
                }
            }
        }

        return converge;
    }

}
