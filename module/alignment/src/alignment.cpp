#include <iostream>
#include <iomanip>
#include <Eigen/LU>
#include "alignment.hpp"

namespace vk{


bool align2D(const cv::Mat& cur_img, const cv::Mat& ref_patch, const cv::Mat& ref_patch_gx, const cv::Mat& ref_patch_gy, 
    cv::Point2f& cur_px_estimate, const int MAX_ITER, const float EPS)
{
    const int cols = ref_patch.cols;
    const int rows = ref_patch.rows;
    const float min_update_squared = EPS*EPS;
    bool converged = false;

    cv::Mat H = cv::Mat::zeros(3,3,CV_32FC1);
    for(int y = 0; y < rows; ++y)
    {
        const float* gx_ref = ref_patch_gx.ptr<float>(y);
        const float* gy_ref = ref_patch_gy.ptr<float>(y);
        for(int x = 0; x < cols; ++x)
        {
            cv::Mat J = (cv::Mat_<float>(1, 3) << *(gx_ref+x), *(gy_ref+x), 1.0f);
            H += J.t() * J;
        }
    }
    cv::Mat invH = H.inv();

    //! temp parameters
    float u = cur_px_estimate.x;
    float v = cur_px_estimate.y;

    const int cur_step = cur_img.step.p[0];
    float intensity_diff = 0;

#ifdef _DEBUG_MODE_
    cv::Mat warpI = cv::Mat(rows, cols, CV_32FC1);
#endif

#ifdef _OUTPUT_MESSAGES_
    std::cout << "== Alignment::align2D, input point: [" << std::setiosflags(std::ios::fixed) << std::setprecision(3) << u << ", " << v << "], start iteration:\n";
#endif

    int iter = 0;
    while(iter++ < MAX_ITER)
    {
        cv::Mat Jres = cv::Mat::zeros(3, 1, CV_32FC1);
        cv::Mat dp = cv::Mat::zeros(3, 1, CV_32FC1);

        cv::Point2f cur_patch_LT(u - floor(cols / 2), v - floor(rows / 2));
        if(cur_patch_LT.x < 0 || cur_patch_LT.y < 0 || cur_patch_LT.x+ cols > cur_img.cols || cur_patch_LT.y+rows > cur_img.rows)
            break;

        // compute interpolation weights
        const int u_r = floor(u);
        const int v_r = floor(v);
        const float subpix_x = u - u_r;
        const float subpix_y = v - v_r;
        const float wTL = (1.0 - subpix_x)*(1.0 - subpix_y);
        const float wTR = subpix_x * (1.0 - subpix_y);
        const float wBL = (1.0 - subpix_x)*subpix_y;
        const float wBR = subpix_x * subpix_y;

        float mean_error = 0;
        for(int y = 0; y < rows; ++y)
        {
            const float* i_ref = ref_patch.ptr<float>(y);
            const float* gx_ref = ref_patch_gx.ptr<float>(y);
            const float* gy_ref = ref_patch_gy.ptr<float>(y);
            const uint8_t* i_cur = (uint8_t*)(cur_img.ptr<uint8_t>(y + v_r - rows / 2) + u_r - cols / 2);
            for(int x = 0; x < cols; ++x)
            {
                const float cur_intensity = wTL*i_cur[x] + wTR*i_cur[x + 1] + wBL*i_cur[cur_step + x] + wBR*i_cur[cur_step + x + 1];
                const float residual = cur_intensity - i_ref[x] + intensity_diff;

#ifdef _DEBUG_MODE_
                warpI.at<float>(y, x) = cur_intensity;
#endif

#ifdef _OUTPUT_MESSAGES_
                mean_error += residual*residual;
#endif

                cv::Mat J = (cv::Mat_<float>(1, 3) << *(gx_ref + x), *(gy_ref + x), 1.0f);
                Jres += residual* J.t();
            }
        }

        dp = invH * Jres;
        u -= dp.at<float>(0, 0);
        v -= dp.at<float>(1, 0);
        intensity_diff -= dp.at<float>(2, 0);

#ifdef _OUTPUT_MESSAGES_
        mean_error /= rows*cols;
        std::cout << "* iter: " << std::setw(3) << iter;
        std::cout << ", point: [" << u << ", " << v << "], intensity_diff: " << std::setw(7) << intensity_diff << ", mean_error: " << mean_error << "\n";
#endif

        if(dp.dot(dp) < min_update_squared)
        {
            converged = true;
            break;
        }
    }

    cur_px_estimate = cv::Point2f(u, v);


#ifdef _OUTPUT_MESSAGES_
    std::cout << "== converged: " << converged << ", point: [" << u << ", " << v << "]" << std::endl;
#endif

    return converged;
}


void getPatch(const cv::Mat &src_img, cv::Mat &dst_img, const cv::Point2f &centre, const int size, const cv::Mat &affine)
{
    assert(src_img.type() == CV_8UC1);
    assert(affine.size() == cv::Size(2,2) && affine.type() == CV_32FC1);
    const float half_size = size * 0.5;
    const int src_step = src_img.step.p[0];

    //! get affine parameters
    const float xWarp0 = affine.at<float>(0, 0);
    const float xWarp1 = affine.at<float>(0, 1);
    const float yWarp0 = affine.at<float>(1, 0);
    const float yWarp1 = affine.at<float>(1, 1);

    dst_img = cv::Mat(size, size, CV_32FC1);
    for(size_t y = 0; y < size; y++)
    {
        float *dst = dst_img.ptr<float>(y);
        for(size_t x = 0; x < size; x++)
        {
            //! get affine pixel
            const cv::Point2f pt(x - half_size, y - half_size);
            const float u = xWarp0 * pt.x + xWarp1 * pt.y + centre.x;
            const float v = yWarp0 * pt.x + yWarp1 * pt.y + centre.y;

            //! check border
            if(u < 0 || v < 0 || u >= src_img.cols - 1 || v >= src_img.rows - 1)
                dst[x] = 0;
            
            //! compute interpolation weights
            const int u_r = floor(u);
            const int v_r = floor(v);
            const float subpix_x = u - u_r;
            const float subpix_y = v - v_r;
            const float wTL = (1.0 - subpix_x)*(1.0 - subpix_y);
            const float wTR = subpix_x * (1.0 - subpix_y);
            const float wBL = (1.0 - subpix_x)*subpix_y;
            const float wBR = subpix_x * subpix_y;
        
            const uint8_t *src = src_img.ptr<uint8_t>(v_r) + u_r;
            dst[x] = wTL*src[0] + wTR*src[1] + wBL*src[src_step] + wBR*src[src_step + 1];
        }
    }

}

/**
*   Class Align
*/

Align::Align(const cv::Mat& ref_patch, const cv::Mat& ref_gradx, const cv::Mat& ref_grady, std::vector<std::pair<int, int> > &partern) :
    N(partern.size()), partern_(partern)
{
    ref_patch_.resize(N);
    ref_gradx_.resize(N);
    ref_grady_.resize(N);

    for(size_t i = 0; i < N; i++)
    {
        const int u = partern_[i].first;
        const int v = partern_[i].second;
        partern_[i].first -= ref_patch.cols*0.5;
        partern_[i].second -= ref_patch.rows*0.5;

        ref_patch_[i] = ref_patch.at<float>(v, u);
        ref_gradx_[i]= ref_gradx.at<float>(v, u);
        ref_grady_[i] = ref_grady.at<float>(v, u);
    }
}

void Align::printInfo()
{
    const size_t num = out_info_.size();
    InfoMsg inputMsg = out_info_[0];
    const size_t estimate_size = inputMsg.estimate.size();

    std::string iter_msg = std::string("Iterate");
    std::string estimate_msg = std::string("Estimate");
    std::string error_msg = std::string("Error");
    std::string step_msg = std::string("Step");
    std::string time_msg = std::string("Time");
    std::cout << "-----------------------------------------------------------------------------" << std::endl;
    std::cout << "# Align, input : [" << std::setiosflags(std::ios::fixed) << std::setprecision(3) << inputMsg.estimate.transpose() << "]\n";
    std::cout << " " << iter_msg 
        << std::setw(estimate_size * 9 + 3) << estimate_msg
        << std::setw(12 + 1) << error_msg
        << std::setw(12 + 1) << step_msg
        << std::setw(8 + 1) << time_msg << "\n";

    size_t index = 1;
    for(; index < num; index++)
    {
        InfoMsg &msg = out_info_[index];
        std::cout << "*" << std::setw(7) << msg.id << " [";
        for(size_t n = 0; n < estimate_size; n++) 
        {
            std::cout << std::setiosflags(std::ios::fixed) << std::setprecision(3) << std::setw(9) << msg.estimate[n];
        }
        std::cout << "] "
            << std::setiosflags(std::ios::fixed) << std::setprecision(3) << std::setw(12) << msg.error << " "
            << std::setiosflags(std::ios::fixed) << std::setprecision(8) << std::setw(12) << msg.step << " "
            << std::setiosflags(std::ios::fixed) << std::setprecision(3) << std::setw(8) << 0.001*(msg.t_end-msg.t_start) << "\n";
    }
    index--;

    long long t_count = 0;
    for(size_t j = 1; j < num; j++)
    {
        t_count += out_info_[j].t_end - out_info_[j].t_start;
    }
    t_count /= (num - 1);

    std::cout << "# Converged: " << out_info_[index].converged
        << ", Estimate: [" << std::setiosflags(std::ios::fixed) << std::setprecision(3) << out_info_[index].estimate.transpose() << "]\n"
        << "# Total Time(ms): " << 0.001*(out_info_[index].t_end - out_info_[0].t_start)
        << " Percompute: " << 0.001*(out_info_[0].t_end - out_info_[0].t_start)
        << ", Average Iteration: " << 0.001 * t_count << std::endl;

}

bool Align::run(const cv::Mat& cur_img, Eigen::VectorXd &estimate, const size_t MAX_ITER, const double EPS)
{
    const double min_update_squared = EPS*EPS;
    bool converged = false;
    estimate_ = estimate;

    using namespace std::chrono;

    steady_clock::time_point t0 = steady_clock::now();
    perCompute();
    steady_clock::time_point t1 = steady_clock::now();

    //! Output Information
    {
        out_info_.clear();
        out_info_.reserve(100);
        InfoMsg msg;
        msg.id = 0;
        msg.estimate = estimate_;
        msg.t_start = duration_cast<microseconds>(t0.time_since_epoch()).count();
        msg.t_end  = duration_cast<microseconds>(t1.time_since_epoch()).count();
        out_info_.push_back(msg);
    }

    size_t iter = 0;
    while(iter++ < MAX_ITER)
    {
        steady_clock::time_point t2 = steady_clock::now();
        const double mean_error = computeResiduals(cur_img);

        const double step_squared = update();
        steady_clock::time_point t3 = steady_clock::now();

        //! Output Information
        {
            InfoMsg msg;
            msg.id = iter;
            msg.error = mean_error;
            msg.step = step_squared;
            msg.estimate = estimate_;
            msg.t_start = duration_cast<microseconds>(t2.time_since_epoch()).count();
            msg.t_end = duration_cast<microseconds>(t3.time_since_epoch()).count();

            out_info_.push_back(msg);
        }

        if(step_squared < min_update_squared)
        {
            converged = true;
            break;
        }
    }
    estimate = estimate_;

    //! Output Information
    out_info_[out_info_.size() - 1].converged = converged;


    return converged;
}

/**
*   Class Align2DI
*/

void Align2DI::perCompute()
{
    H_.resize(3, 3);
    Jac_.resize(3, N);
    Jres_.resize(3);
    H_.setZero();
    Jac_.setZero();
    
    for(size_t i = 0; i < N; i++)
    {
        Eigen::Vector3d J;
        J[0] = ref_gradx_[i];
        J[1] = ref_grady_[i];
        J[2] = 1;
        Jac_.col(i) = J;
        H_.noalias() += J*J.transpose();
    }

    Hinv_ = H_.inverse();

}

const double Align2DI::computeResiduals(const cv::Mat &cur_img)
{
    Eigen::Vector3d J;
    const double u = estimate_[0];
    const double v = estimate_[1];
    const double idiff = estimate_[2];
    // compute interpolation weights
    const int u_r = floor(u);
    const int v_r = floor(v);
    const float subpix_x = u - u_r;
    const float subpix_y = v - v_r;
    const float wTL = (1.0 - subpix_x)*(1.0 - subpix_y);
    const float wTR = subpix_x * (1.0 - subpix_y);
    const float wBL = (1.0 - subpix_x)*subpix_y;
    const float wBR = subpix_x * subpix_y;

    const int cur_step = cur_img.step.p[0];
    double mean_error = 0;
    Jres_.setZero();

    for(size_t i = 0; i < N; i++)
    {
        const uint8_t* i_cur = (uint8_t*)(cur_img.ptr<uint8_t>(partern_[i].second + v_r) + partern_[i].first + u_r);
        const double cur_intensity = wTL*i_cur[0] + wTR*i_cur[1] + wBL*i_cur[cur_step] + wBR*i_cur[cur_step + 1];
        const double residual = cur_intensity - ref_patch_[i] + idiff;

        mean_error += residual*residual;

        J = Jac_.col(i);
        Jres_.noalias() += J * residual;
    }
    mean_error /= N;

    return mean_error;
}

const double Align2DI::update()
{
    Eigen::Vector3d dp = Hinv_ * Jres_;
    estimate_[0] -= dp[0];
    estimate_[1] -= dp[1];
    estimate_[2] -= dp[2];

    return dp.dot(dp);
}

}//! namespace vk