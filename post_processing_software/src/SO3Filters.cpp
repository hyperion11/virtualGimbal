#include "SO3Filters.h"

void gradientLimit(Eigen::VectorXd &input, double maximum_gradient_)
{
    if (input.rows() < 2)
        return;
    double limited_value = input.head(1)[0];
    for (int i = 1, e = input.rows(); i < e; ++i)
    {
        if (input(i) > limited_value - maximum_gradient_)
        {
            limited_value = input(i);
        }
        else
        {
            limited_value -= maximum_gradient_;
            input(i) = limited_value;
        }
    }
    limited_value = input.tail(1)[0];
    for (int i = input.rows() - 2; i >= 0; --i)
    {
        if (input(i) > limited_value - maximum_gradient_)
        {
            limited_value = input(i);
        }
        else
        {
            limited_value -= maximum_gradient_;
            input(i) = limited_value;
        }
    }
}

/**
     * @brief ワープした時に欠けがないかチェックします
     * @retval false:欠けあり true:ワープが良好
     **/
bool isGoodWarp(std::vector<Eigen::Array2d, Eigen::aligned_allocator<Eigen::Array2d>> &contour)
{
    for (const auto &p : contour)
    {
        // if ((abs(contour[i]) < 1.0) && (abs(contour[i + 1]) < 1.0))
        if ((p.abs() < 1.0).all())
        {
            return false;
        }
    }
    return true;
}

std::vector<Eigen::Array2d, Eigen::aligned_allocator<Eigen::Array2d>> getSparseContour(VideoPtr video_info, int n)
{
    std::vector<Eigen::Array2d, Eigen::aligned_allocator<Eigen::Array2d>> contour;
    Eigen::Array2d point;
    Eigen::Array2d U_max;
    double u_max = video_info->camera_info->width_ - 1.0;
    double v_max = video_info->camera_info->height_ - 1.0;
    // Top
    for (int i = 0; i <= n; ++i)
    {
        point << (double)i / (double)n * u_max, 0.0;
        contour.push_back(point);
    }
    // Bottom
    for (int i = 0; i <= n; ++i)
    {
        point << (double)i / (double)n * u_max, v_max;
        contour.push_back(point);
    }

    // Left
    for (int i = 1; i < n - 1; ++i)
    {
        point << 0.0, (double)i / (double)n * v_max;
        contour.push_back(point);
    }
    // Right
    for (int i = 0; i < n - 1; ++i)
    {
        point << u_max, (double)i / (double)n * v_max;
        contour.push_back(point);
    }
    return contour;
}

/** @brief 補正前の画像座標から、補正後のポリゴンの頂点を作成
 * @param [in]	Qa	ジャイロの角速度から計算したカメラの方向を表す回転クウォータニオン時系列データ、参照渡し
 * @param [in]	Qf	LPFを掛けて平滑化した回転クウォータニオンの時系列データ、参照渡し
 * @param [in]	m	画面の縦の分割数[ ]
 * @param [in]	n	画面の横の分割数[ ]
 * @param [in]	IK	"逆"歪係数(k1,k2,p1,p2)
 * @param [in]	matIntrinsic	カメラ行列(fx,fy,cx,cy) [pixel]
 * @param [in]	imageSize	フレーム画像のサイズ[pixel]
 * @param [in]  adjustmentQuaternion 画面方向を微調整するクォータニオン[rad]
 * @param [out]	vecPorigonn_uv	OpenGLのポリゴン座標(u',v')座標(-1~1)の組、歪補正後の画面を分割した時の一つ一つのポリゴンの頂点の組
 * @param [in]	zoom	倍率[]。拡大縮小しないなら1を指定すること。省略可
 * @retval true:成功 false:折り返し発生で失敗
 **/
void getUndistortUnrollingContour(
    double time,
    RotationQuaternionPtr rotation_quaternion,
    std::vector<Eigen::Array2d, Eigen::aligned_allocator<Eigen::Array2d>> &contour,
    double zoom,
    VideoPtr video_param,
    Eigen::VectorXd &filter_coeffs)
{
    //手順
    //1.補正前画像を分割した時の分割点の座標(pixel)を計算
    //2.1の座標を入力として、各行毎のW(t1,t2)を計算
    //3.補正後の画像上のポリゴン座標(pixel)を計算、歪み補正も含める
    double &line_delay = video_param->camera_info->line_delay_;
    Eigen::Array2d f, c;
    f << video_param->camera_info->fx_, video_param->camera_info->fy_;
    c << video_param->camera_info->cx_, video_param->camera_info->cy_;
    const double &ik1 = video_param->camera_info->inverse_k1_;
    const double &ik2 = video_param->camera_info->inverse_k2_;
    const double &ip1 = video_param->camera_info->inverse_p1_;
    const double &ip2 = video_param->camera_info->inverse_p2_;

    contour = getSparseContour(video_param, 9);
    Eigen::MatrixXd R;
    Eigen::Array2d x1;
    Eigen::Vector3d x3, xyz;
    for (auto &p : contour)
    {
        double time_in_row = line_delay * (p[1] - video_param->camera_info->height_ * 0.5);
        //↓まちがってる。本来はフィルタされたカメラ姿勢とフィルタ後の姿勢の差分が必要。
        // R = (rotation_quaternion->getRotationQuaternion(time_in_row + time).conjugate() * rotation_quaternion->getRotationQuaternion(time)).matrix();
        //↓これでいい
        R = rotation_quaternion->getCorrectionQuaternion(time_in_row,filter_coeffs).matrix();
        //↑
        x1 = (p - c) / f;
        double r = x1.matrix().norm();
        Eigen::Array2d x2 = x1 * (1.0 + ik1 * pow(r, 2.0) + ik2 * pow(r, 4.0));
        x2[0] += 2.0 * ip1 * x1[0] * x1[1] + ip2 * (pow(r, 2.0) + 2 * pow(x1[0], 2.0));
        x2[1] += ip1 * (pow(r, 2.0) + 2.0 * pow(x1[1], 2.0)) + 2.0 * ip2 * x1[0] * x1[1];
        //折り返し防止
        if (((x2 - x1).abs() > 1).any())
        {
            printf("Warning: Turn backing.\n");
            x2 = x1;
        }
        x3 << x2[0], x2[1], 1.0;
        xyz = R * x3;
        x2 << xyz[0] / xyz[2], xyz[1] / xyz[2];
        contour.push_back(x2 * f * zoom + c);
    }
}

bool hasBlackSpace(double time,
                   RotationQuaternionPtr rotation_quaternion,
                   double zoom,
                   VideoPtr video_param)
{
    std::vector<Eigen::Array2d, Eigen::aligned_allocator<Eigen::Array2d>> contour;
    Eigen::VectorXd filter_coeff = getKaiserWindow()//★フィルタクラス作ろう！
    getUndistortUnrollingContour(time, rotation_quaternion, contour, zoom, video_param);
    return !isGoodWarp(contour);
}



Eigen::VectorXd getKaiserWindow(uint32_t tap_length, uint32_t alpha, bool swap){
    Eigen::VectorXd window = Eigen::VectorXd::Zero(tap_length);

    if(tap_length % 2){ //奇数
        int32_t L = tap_length/2;
        for(int32_t n=-L,e=L;n<=e;++n){
            window[n+L] = boost::math::cyl_bessel_i(0.0,alpha*sqrt(1.0-pow((double)n/(double)L,2.0)))
                    /boost::math::cyl_bessel_i(0.0,alpha);
        }
    }else{  //偶数
        int32_t L = tap_length/2;
        for(int32_t n=-L,e=L;n<e;++n){//異なる終了条件
            window[n+L] = boost::math::cyl_bessel_i(0.0,alpha*sqrt(1.0-pow((double)n/(double)L,2.0)))
                    /boost::math::cyl_bessel_i(0.0,alpha);
        }
    }

    if(true == swap){
        Eigen::VectorXd buff2(window.rows());
        buff2.block(0,0,window.rows()/2,1) = window.block(window.rows()/2,0,window.rows()/2,1);
        buff2.block(window.rows()/2,0,window.rows()-window.rows()/2,1) = window.block(0,0,window.rows()-window.rows()/2,1);

        return buff2;
    }else{
        return window;
    }
}


uint32_t bisectionMethod(double time, int32_t minimum_filter_strength, int32_t maximum_filter_strength, int max_iteration, uint32_t eps)
{
    int32_t a = minimum_filter_strength;
    int32_t b = maximum_filter_strength;
    int count = 0;
    int32_t m;
    //    while(hasBlackSpace(maximum_filter_strength,frame)){
    //        minimum_filter_strength = maximum_filter_strength;
    //        maximum_filter_strength *= 2;
    //    }
    while ((abs(a - b) > eps) && (count++ < max_iteration))
    {
        m = (a + b) * 0.5;
        if (hasBlackSpace(a, time) ^ hasBlackSpace(m, time))
        {
            b = m;
        }
        else
        {
            a = m;
        }
        if (count == max_iteration)
        {
            std::cout << "max_iteration" << std::endl;
        }
    }
    return m;
}

// Eigen::VectorXd getFilterCoefficients(int32_t minimum_filter_strength, int32_t maximum_filter_strength)
// {
//     Eigen::VectorXd filter_strength(raw_quaternion.rows());
//     //Calcurate in all frame
//     for (int frame = 0, e = filter_strength.rows(); frame < e; ++frame)
//     {
//         if (hasBlackSpace(maximum_filter_strength, frame))
//         {
//             filter_strength[frame] = maximum_filter_strength;
//         }
//         else if (!hasBlackSpace(minimum_filter_strength, frame))
//         {
//             filter_strength[frame] = minimum_filter_strength;
//         }
//         else
//         {
//             filter_strength[frame] = bisectionMethod(frame, minimum_filter_strength, maximum_filter_strength);
//         }
//     }
//     //    std::cout << filter_strength << std::endl;
//     gradientLimit(filter_strength);

//     return (filter_strength);
// }