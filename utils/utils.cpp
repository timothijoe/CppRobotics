#include "utils.hpp"
#include "matplotlibcpp.h"

using namespace Eigen;
namespace plt = matplotlibcpp;

static void plot(VectorXd vec_x, VectorXd vec_y, std::string style)
{
    std::vector<double> x;
    std::vector<double> y;
    double* x_pointer = vec_x.data();
    double* y_pointer = vec_y.data();
    x.assign(x_pointer, x_pointer + vec_x.rows());
    y.assign(y_pointer, y_pointer + vec_y.rows());

    plt::plot(x, y, style);
}

namespace utils {

void draw_arrow(double x, double y, double theta, double L, std::string color)
{
    double angle = M_PI / 6;
    double d = 0.3 * L;

    double x_start = x;
    double y_start = y;
    double x_end = x + L * cos(theta);
    double y_end = y + L * sin(theta);

    double theta_hat_L = theta + M_PI - angle;
    double theta_hat_R = theta + M_PI + angle;

    double x_hat_start = x_end;
    double x_hat_end_L = x_hat_start + d * cos(theta_hat_L);
    double x_hat_end_R = x_hat_start + d * cos(theta_hat_R);

    double y_hat_start = y_end;
    double y_hat_end_L = y_hat_start + d * sin(theta_hat_L);
    double y_hat_end_R = y_hat_start + d * sin(theta_hat_R);

    plt::plot({x_start, x_end}, {y_start, y_end}, color);
    plt::plot({x_hat_start, x_hat_end_L}, {y_hat_start, y_hat_end_L}, color);
    plt::plot({x_hat_start, x_hat_end_R}, {y_hat_start, y_hat_end_R}, color);
}

void draw_vehicle(Vector3d state, double steer,
    VehicleConfig c, std::string color, bool draw_wheel, bool show_arrow)
{
    Matrix<double, 2, 5> vehicle;
    Matrix<double, 2, 5> wheel;
    vehicle << -c.RB, -c.RB, c.RF, c.RF, -c.RB,
                c.W / 2, -c.W / 2, -c.W / 2, c.W / 2, c.W / 2;
    wheel << -c.TR, -c.TR, c.TR, c.TR, -c.TR,
            c.TW / 4, -c.TW / 4, -c.TW / 4, c.TW / 4, c.TW / 4;

    Matrix<double, 2, 5> rlWheel = wheel;
    Matrix<double, 2, 5> rrWheel = wheel;
    Matrix<double, 2, 5> frWheel = wheel;
    Matrix<double, 2, 5> flWheel = wheel;

    Matrix2d rot1;
    Matrix2d rot2;
    rot1 << cos(state[2]), -sin(state[2]), sin(state[2]), cos(state[2]);
    rot2 << cos(steer), -sin(steer), sin(steer), cos(steer);

    vehicle = rot1 * vehicle;
    vehicle += Vector2d(state[0], state[1]).replicate(1, 5);
    plot(vehicle.row(0), vehicle.row(1), color);

    if (draw_wheel) {
        frWheel = rot2 * frWheel;
        flWheel = rot2 * flWheel;
    
        frWheel += Vector2d(c.WB, -c.WD / 2).replicate(1, 5);
        flWheel += Vector2d(c.WB, c.WD / 2).replicate(1, 5);
    
        rrWheel.row(1) -= VectorXd::Constant(5, c.WD / 2);
        rlWheel.row(1) += VectorXd::Constant(5, c.WD / 2);
    
        frWheel = rot1 * frWheel;
        flWheel = rot1 * flWheel;
        rrWheel = rot1 * rrWheel;
        rlWheel = rot1 * rlWheel;
        
        frWheel += Vector2d(state[0], state[1]).replicate(1, 5);
        flWheel += Vector2d(state[0], state[1]).replicate(1, 5);
        rrWheel += Vector2d(state[0], state[1]).replicate(1, 5);
        rlWheel += Vector2d(state[0], state[1]).replicate(1, 5);
        
        plot(frWheel.row(0), frWheel.row(1), color);
        plot(flWheel.row(0), flWheel.row(1), color);
        plot(rrWheel.row(0), rrWheel.row(1), color);
        plot(rlWheel.row(0), rlWheel.row(1), color);

        draw_arrow(state(0), state(1), state(2), c.WB * 0.8, color);
    }
}

}
