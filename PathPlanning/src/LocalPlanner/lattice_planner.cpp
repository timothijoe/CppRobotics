#include <fmt/core.h>

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "cubic_spline.hpp"
#include "matplotlibcpp.h"
#include "quartic_polynomial.hpp"
#include "quintic_polynomial.hpp"
#include "road_line.hpp"
#include "utils.hpp"

using std::vector;
using namespace Eigen;
//using namespace utils;
namespace plt = matplotlibcpp;

constexpr double ROAD_WIDTH = 8.0;
constexpr double ROAD_SAMPLE_STEP = 1.0;
constexpr double TARGET_SPEED = 30.0 / 3.6;
constexpr double SPEED_SAMPLE_STEP = 5.0 / 3.6;

constexpr double T_STEP = 0.15;
constexpr double K_JERK = 0.1;
constexpr double K_TIME = 1.0;
constexpr double K_V_DIFF = 1.0;
constexpr double K_OFFSET = 1.5;
constexpr double K_COLLISION = 500;

constexpr double MAX_SPEED = 50.0 / 3.6;
constexpr double MAX_ACCEL = 8.0;
constexpr double MAX_CURVATURE = 6.0;

static void plot(VectorXd vec_x, VectorXd vec_y, std::string style) {
    std::vector<double> x;
    std::vector<double> y;
    double* x_pointer = vec_x.data();
    double* y_pointer = vec_y.data();
    x.assign(x_pointer, x_pointer + vec_x.rows());
    y.assign(y_pointer, y_pointer + vec_y.rows());

    plt::plot(x, y, style);
}

void draw_arrow(double x, double y, double theta, double L, std::string color) {
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

void draw_vehicle1(Vector3d state, double steer, utils::VehicleConfig c, std::string color= "-k", bool show_wheel = true,
                  bool show_arrow= true) {
    Matrix<double, 2, 5> vehicle;
    Matrix<double, 2, 5> wheel;
    vehicle << -c.RB, -c.RB, c.RF, c.RF, -c.RB, c.W / 2, -c.W / 2, -c.W / 2, c.W / 2, c.W / 2;
    wheel << -c.TR, -c.TR, c.TR, c.TR, -c.TR, c.TW / 4, -c.TW / 4, -c.TW / 4, c.TW / 4, c.TW / 4;

    Matrix<double, 2, 5> rlWheel = wheel;
    Matrix<double, 2, 5> rrWheel = wheel;
    Matrix<double, 2, 5> frWheel = wheel;
    Matrix<double, 2, 5> flWheel = wheel;

    if (steer > c.MAX_STEER) {
        steer = c.MAX_STEER;
    }

    Matrix2d rot1;
    Matrix2d rot2;
    rot1 << cos(state[2]), -sin(state[2]), sin(state[2]), cos(state[2]);
    rot2 << cos(steer), -sin(steer), sin(steer), cos(steer);

    vehicle = rot1 * vehicle;
    vehicle += Vector2d(state[0], state[1]).replicate(1, 5);
    plot(vehicle.row(0), vehicle.row(1), color);

    if (show_wheel) {
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
    }
    if (show_arrow) {
        draw_arrow(state[0], state[1], state[2], c.WB * 0.8, color);
    }
}

class Path {
public:
    vector<double> t;
    double cost = 0.0;

    vector<double> l;
    vector<double> l_v;
    vector<double> l_a;
    vector<double> l_jerk;

    vector<double> s;
    vector<double> s_v;
    vector<double> s_a;
    vector<double> s_jerk;

    vector<double> x;
    vector<double> y;
    vector<double> yaw;
    vector<double> ds;
    vector<double> curv;

    Path() {}
    ~Path() {}

    void SL_2_XY(CubicSpline2D& ref_path);
    void calc_yaw_curv(void);
    bool operator<(const Path& other) const { return cost < other.cost; }
};

void Path::SL_2_XY(CubicSpline2D& ref_path) {
    x.clear();
    y.clear();

    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] > ref_path.s.back()) {
            break;
        }

        Vector2d xy_ref = ref_path.calc_position(s[i]);
        double yaw = ref_path.calc_yaw(s[i]);
        double x_ref = xy_ref[0] + l[i] * cos(yaw + M_PI_2);
        double y_ref = xy_ref[1] + l[i] * sin(yaw + M_PI_2);

        x.push_back(x_ref);
        y.push_back(y_ref);
    }
}

void Path::calc_yaw_curv(void) {
    yaw.clear();
    curv.clear();
    ds.clear();

    for (size_t i = 0; i + 1 < x.size(); ++i) {
        double dx = x[i + 1] - x[i];
        double dy = y[i + 1] - y[i];
        ds.push_back(hypot(dx, dy));
        yaw.push_back(atan2(dy, dx));
    }

    if (yaw.empty()) {
        return;
    }
    yaw.push_back(yaw.back());
    ds.push_back(ds.back());

    for (size_t i = 0; i + 1 < yaw.size(); ++i) {
        curv.emplace_back((yaw[i + 1] - yaw[i]) / ds[i]);
    }
}

vector<vector<double>> get_reference_line(vector<double> cx, vector<double> cy,
                                          CubicSpline2D& spline) {
    vector<double> x;
    vector<double> y;
    for (size_t idx = 0; idx < cx.size(); idx += 3) {
        x.push_back(cx[idx]);
        y.push_back(cy[idx]);
    }

    vector<vector<double>> traj = CubicSpline2D::calc_spline_course(x, y, 0.1);
    spline = CubicSpline2D(x, y);

    return traj;
}

bool verify_path(const Path& path) {
    for (size_t i = 0; i < path.s_v.size(); ++i) {
        if (path.s_v[i] > MAX_SPEED || abs(path.s_a[i]) > MAX_ACCEL ||
            abs(path.curv[i]) > MAX_CURVATURE) {
            return false;
        }
    }

    return true;
}

double is_path_collision(const Path& path, const utils::VehicleConfig& vc,
                         const vector<vector<double>>& obs) {
    vector<double> x;
    vector<double> y;
    vector<double> yaw;
    for (size_t i = 0; i < path.x.size(); i += 3) {
        x.push_back(path.x[i]);
        y.push_back(path.y[i]);
        yaw.push_back(path.yaw[i]);
    }

    for (size_t i = 0; i < x.size(); ++i) {
        double d = 1.8;
        double dl = (vc.RF - vc.RB) / 2.0;
        double r = hypot((vc.RF + vc.RB) / 2.0, vc.W / 2.0) + d;

        double cx = x[i] + dl * cos(yaw[i]);
        double cy = y[i] + dl * sin(yaw[i]);

        for (size_t j = 0; j < obs[0].size(); ++j) {
            double xo = obs[0][j] - cx;
            double yo = obs[1][j] - cy;
            double dx = xo * cos(yaw[i]) + yo * sin(yaw[i]);
            double dy = -xo * sin(yaw[i]) + yo * cos(yaw[i]);

            if (abs(dx) < r && abs(dy) < vc.W / 2 + d) {
                return 1.0;
            }
        }
    }

    return 0.0;
}

vector<Path> sampling_paths(double l0, double l0_v, double l0_a, double s0, double s0_v,
                            double s0_a, CubicSpline2D& ref_path, const utils::VehicleConfig& vc,
                            const vector<vector<double>>& obs) {
    vector<Path> paths;

    for (double s1_v = TARGET_SPEED * 0.6; s1_v < TARGET_SPEED * 1.4; s1_v += TARGET_SPEED * 0.2) {
        for (double t1 = 4.5; t1 < 5.5; t1 += 0.2) {
            Path path_pre;
            QuarticPolynomial path_lon(s0, s0_v, s0_a, s1_v, 0.0, t1);

            for (double t = 0.0; t < t1; t += T_STEP) {
                path_pre.t.push_back(t);
                path_pre.s.push_back(path_lon.calc_point(t));
                path_pre.s_v.push_back(path_lon.calc_first_derivative(t));
                path_pre.s_a.push_back(path_lon.calc_second_derivative(t));
                path_pre.s_jerk.push_back(path_lon.calc_third_derivative(t));
            }

            for (double l1 = -ROAD_WIDTH; l1 < ROAD_WIDTH; l1 += ROAD_SAMPLE_STEP) {
                Path path = path_pre;
                QuinticPolynomial path_lat(l0, l0_v, l0_a, l1, 0.0, 0.0, t1);

                for (double t : path_pre.t) {
                    path.l.push_back(path_lat.calc_point(t));
                    path.l_v.push_back(path_lat.calc_first_derivative(t));
                    path.l_a.push_back(path_lat.calc_second_derivative(t));
                    path.l_jerk.push_back(path_lat.calc_third_derivative(t));
                }

                path.SL_2_XY(ref_path);
                path.calc_yaw_curv();
                if (path.yaw.empty()) {
                    continue;
                }

                double l_jerk_sum = 0.0;
                double s_jerk_sum = 0.0;
                double v_diff = abs(TARGET_SPEED - path.s_v.back());
                for (size_t i = 0; i < path.l_jerk.size(); ++i) {
                    l_jerk_sum += abs(path.l_jerk[i]);
                    s_jerk_sum += abs(path.s_jerk[i]);
                }

                path.cost = K_JERK * (l_jerk_sum + s_jerk_sum) + K_V_DIFF * v_diff +
                            K_TIME * t1 * 2 + K_OFFSET * abs(path.l.back()) +
                            K_COLLISION * is_path_collision(path, vc, obs);

                paths.emplace_back(path);
            }
        }
    }

    return paths;
}

vector<Path> sampling_paths_for_stopping(double l0, double l0_v, double l0_a, double s0,
                                         double s0_v, double s0_a, CubicSpline2D& ref_path) {
    vector<Path> paths;
    vector<double> s1_v_vec = {-2.0, -1.0, 0.0, 1.0, 2.0};

    for (double s1_v : s1_v_vec) {
        for (double t1 = 0.0; t1 < 16.0; t1 += 1.0) {
            Path path_pre;
            QuinticPolynomial path_lon(s0, s0_v, s0_a, 55.0, s1_v, 0.0, t1);

            for (double t = 0.0; t < t1; t += T_STEP) {
                path_pre.t.push_back(t);
                path_pre.s.push_back(path_lon.calc_point(t));
                path_pre.s_v.push_back(path_lon.calc_first_derivative(t));
                path_pre.s_a.push_back(path_lon.calc_second_derivative(t));
                path_pre.s_jerk.push_back(path_lon.calc_third_derivative(t));
            }

            for (double l1 = 0.0; l1 <= 0.1; l1 += ROAD_SAMPLE_STEP) {
                Path path = path_pre;
                QuinticPolynomial path_lat(l0, l0_v, l0_a, l1, 0.0, 0.0, t1);

                for (double t : path_pre.t) {
                    path.l.push_back(path_lat.calc_point(t));
                    path.l_v.push_back(path_lat.calc_first_derivative(t));
                    path.l_a.push_back(path_lat.calc_second_derivative(t));
                    path.l_jerk.push_back(path_lat.calc_third_derivative(t));
                }

                path.SL_2_XY(ref_path);
                path.calc_yaw_curv();
                if (path.yaw.empty()) {
                    continue;
                }

                double l_jerk_sum = 0.0;
                double s_jerk_sum = 0.0;
                double s_v_sum = 0.0;
                double v_diff = pow(path.s_v.back(), 2);
                for (size_t i = 0; i < path.l_jerk.size(); ++i) {
                    l_jerk_sum += abs(path.l_jerk[i]);
                    s_jerk_sum += abs(path.s_jerk[i]);
                    s_v_sum += abs(path.s_v[i]);
                }

                path.cost = K_JERK * (l_jerk_sum + s_jerk_sum) + K_V_DIFF * v_diff +
                            K_TIME * t1 * 2 + K_OFFSET * abs(path.l.back()) + 5.0 * s_v_sum;

                paths.emplace_back(path);
            }
        }
    }

    return paths;
}

Path extract_optimal_path(vector<Path>& paths) {
    Path path;
    if (paths.empty()) {
        return path;
    }

    std::sort(paths.begin(), paths.end());
    for (Path& p : paths) {
        if (verify_path(p)) {
            path = p;
            break;
            ;
        }
    }

    return path;
}

Path lattice_planner(double l0, double l0_v, double l0_a, double s0, double s0_v, double s0_a,
                     CubicSpline2D& ref_path, const utils::VehicleConfig& vc,
                     const vector<vector<double>>& obs) {
    vector<Path> paths = sampling_paths(l0, l0_v, l0_a, s0, s0_v, s0_a, ref_path, vc, obs);
    Path path = extract_optimal_path(paths);

    return path;
}

Path lattice_planner_for_stopping(double l0, double l0_v, double l0_a, double s0, double s0_v,
                                  double s0_a, CubicSpline2D& ref_path) {
    vector<Path> paths = sampling_paths_for_stopping(l0, l0_v, l0_a, s0, s0_v, s0_a, ref_path);
    Path path = extract_optimal_path(paths);

    return path;
}

void cruise_case(const utils::VehicleConfig& vc) {
    CruiseRoadLine cruise_line;
    vector<vector<double>> wxy = cruise_line.design_reference_line();
    vector<vector<double>> inxy = cruise_line.design_boundary_left();
    vector<vector<double>> outxy = cruise_line.design_boundary_right();
    vector<vector<double>> obs = {{50, 96, 70, 40, 25}, {10, 25, 40, 50, 75}};
    CubicSpline2D spline;
    vector<vector<double>> traj = get_reference_line(wxy[0], wxy[1], spline);

    double l0 = 0.0;           // current lateral position [m]
    double l0_v = 0.0;         // current lateral speed [m/s]
    double l0_a = 0.0;         // current lateral acceleration [m/s]
    double s0 = 0.0;           // current course position
    double s0_v = 30.0 / 3.6;  // current speed [m/s]
    double s0_a = 0.0;

    while (true) {
        // Path path = lattice_planner(l0, l0_v, l0_a, s0, s0_v, s0_a, spline, vc, obs);
        vector<Path> paths = sampling_paths(l0, l0_v, l0_a, s0, s0_v, s0_a, spline, vc, obs);
        Path path = extract_optimal_path(paths);

        if (path.x.empty()) {
            fmt::print("No feasible path found!!\n");
            break;
        }

        l0 = path.l[1];
        l0_v = path.l_v[1];
        l0_a = path.l_a[1];
        s0 = path.s[1];
        s0_v = path.s_v[1];
        s0_a = path.s_a[1];

        if (hypot(path.x[1] - traj[0].back(), path.y[1] - traj[1].back()) <= 2.0) {
            fmt::print("Goal\n");
            break;
        }

        double dy = (path.yaw[2] - path.yaw[1]) / path.ds[1];
        double steer = utils::pi_2_pi(atan(1.2 * vc.WB * dy));

        plt::cla();
        plt::named_plot("Candidate trajectories", paths[0].x, paths[0].y, "-c");
        for (size_t i = 1; i < paths.size(); i += (paths.size() / 10)) {
            plt::plot(paths[i].x, paths[i].y, "-c");
        }
        plt::plot(wxy[0], wxy[1], {{"linestyle", "--"}, {"color", "gray"}});
        plt::plot(inxy[0], inxy[1], {{"linewidth", "2"}, {"color", "k"}});
        plt::plot(outxy[0], outxy[1], {{"linewidth", "2"}, {"color", "k"}});
        plt::named_plot("Optimal trajectory", path.x, path.y, "-r");
        plt::plot(obs[0], obs[1], "ok");
        //utils::draw_vehicle({path.x[1], path.y[1], path.yaw[1]}, steer, vc);
        draw_vehicle1({path.x[1], path.y[1], path.yaw[1]}, steer, vc);
        plt::title("Lattice Planner in Cruising Scene V[km/h]:" +
                   std::to_string(s0_v * 3.6).substr(0, 4));
        plt::axis("equal");
        plt::legend();
        plt::pause(0.0001);
    }
    plt::show();
}

void stop_case(const utils::VehicleConfig& vc) {
    StopRoadLine stop_line;
    vector<vector<double>> wxy = stop_line.design_reference_line();
    vector<vector<double>> upxy = stop_line.design_boundary_left();
    vector<vector<double>> downxy = stop_line.design_boundary_right();
    CubicSpline2D spline;
    vector<vector<double>> traj = get_reference_line(wxy[0], wxy[1], spline);

    double l0 = 0.0;           // current lateral position [m]
    double l0_v = 0.0;         // current lateral speed [m/s]
    double l0_a = 0.0;         // current lateral acceleration [m/s]
    double s0 = 0.0;           // current course position
    double s0_v = 30.0 / 3.6;  // current speed [m/s]
    double s0_a = 0.0;

    while (true) {
        Path path = lattice_planner_for_stopping(l0, l0_v, l0_a, s0, s0_v, s0_a, spline);

        if (path.x.empty()) {
            fmt::print("No feasible path found!!\n");
            break;
        }

        l0 = path.l[1];
        l0_v = path.l_v[1];
        l0_a = path.l_a[1];
        s0 = path.s[1];
        s0_v = path.s_v[1];
        s0_a = path.s_a[1];
        if (hypot(path.x[1] - 56.0, path.y[1] - 0.0) <= 2.0) {
            fmt::print("Goal\n");
            break;
        }

        double dy = (path.yaw[2] - path.yaw[1]) / path.ds[1];
        double steer = utils::pi_2_pi(atan(1.2 * vc.WB * dy));

        plt::cla();
        plt::plot(wxy[0], wxy[1], {{"linestyle", "--"}, {"color", "gray"}});
        plt::plot(upxy[0], upxy[1], {{"linewidth", "2"}, {"color", "k"}});
        plt::plot(downxy[0], downxy[1], {{"linewidth", "2"}, {"color", "k"}});
        plt::named_plot("Optimal trajectory", path.x, path.y, "-r");
        //utils::draw_vehicle({path.x[1], path.y[1], path.yaw[1]}, steer, vc);
        plt::title("Lattice Planner in Stopping Scene V[km/h]:" +
                   std::to_string(s0_v * 3.6).substr(0, 4));
        plt::axis("equal");
        plt::legend();
        plt::pause(0.0001);
    }
    plt::show();
}

int main(int argc, char** argv) {
    utils::VehicleConfig vc;
    vc.RF = 6.75;
    vc.RB = 1.5;
    vc.W = 4.5;
    vc.WD = 0.7 * vc.W;
    vc.WB = 5.25;
    vc.TR = 0.75;
    vc.TW = 1.5;

    if (argc > 1) {
        stop_case(vc);
    } else {
        cruise_case(vc);
    }

    return 0;
}
