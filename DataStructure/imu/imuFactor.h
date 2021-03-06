#ifndef IMUFACTOR_H
#define IMUFACTOR_H

#include <memory>

#include "IMUMeasure.h"

class viFrame;

class imuFactor {
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
public:
    friend class InitialImpl;

public:
    typedef std::shared_ptr<viFrame>          connection_t;

public:
    typedef Eigen::Matrix<double, 15, 3>   FacJBias_t;  ///< dRdb_g, dvdb_a dvdb_g dpdb_a dpdb_g
    typedef Eigen::Vector3d                speed_t;
    typedef Eigen::Matrix<double, 6, 1>    bias_t;      ///< b_g, b_a

public:
    imuFactor(const IMUMeasure::Transformation& pose,
              const FacJBias_t &jbias, const speed_t &speed, const IMUMeasure::covariance_t &var);
    bool checkConnect(const connection_t &from, const connection_t &to);
    void makeConncect(const connection_t &from, const connection_t &to);
    const IMUMeasure::Transformation& getPoseFac() {
        return deltaPose;
    }

    const speed_t& getSpeedFac() {
        return deltaSpeed;
    }

    const FacJBias_t& getJBias() {
        return JBias;
    }

	const IMUMeasure::covariance_t &getVar() {
		return var;
	}

private:
    int                          id;
    IMUMeasure::Transformation   deltaPose;
    speed_t                      deltaSpeed;
    FacJBias_t                   JBias;
    IMUMeasure::covariance_t     var;
    connection_t                 from;
    connection_t                 to;
};

#endif // IMUFACTOR_H
