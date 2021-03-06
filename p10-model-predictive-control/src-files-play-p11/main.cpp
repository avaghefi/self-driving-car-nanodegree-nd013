#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"
#include <math.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include "spline.h"


// for convenience
using json = nlohmann::json;
using namespace std ;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

int main( int argc, const char *argv[] ) {
  
  uWS::Hub h;

  /****************************************
   * 1. Initialize mpc class
   ****************************************/
  MPC mpc;

  vector<double> hyper_parameters ;


  if (argc != 8 ) {
    cout << " Usage ./mpc ref_cte ref_epsi v val_throttle coeff_cost_ref_val_steering seq_throttle seq_steering \n ie  ./mpc 20 20 1 8 1100 16 600" << endl ;
    return -1 ;
  }
  else {

    for (int i = 1; i < 8; i++) {
      hyper_parameters.push_back( strtod( argv[i], NULL ) ) ;
      cout << strtod( argv[i], NULL ) << endl  ;
    }
  }

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../highway_map.csv";  // CHANGE BACK to ../ after debugging
                        // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    istringstream iss(line);
    double x, y;
    double s, d_x, d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  tk::spline spline_x, spline_y;
  spline_x.set_points(map_waypoints_s, map_waypoints_x);
  spline_y.set_points(map_waypoints_s, map_waypoints_y);

  vector<double> map_waypoints_x_high_res;
  vector<double> map_waypoints_y_high_res;

  // refine path with spline.
  int spline_samples = 6000;
  for (size_t i = 0; i < spline_samples; ++i) {
    map_waypoints_x_high_res.push_back(spline_x(i));
    map_waypoints_y_high_res.push_back(spline_y(i));
    //MAP->waypoints_s_upsampled.push_back(i);
  }

  int counter_thing = 330;

  mpc.Init(hyper_parameters) ;


  h.onMessage([&](uWS::WebSocket<uWS::SERVER> *ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    cout << sdata << endl;
    
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object


          /****************************************
           * 2. Collect data from simulator
           ****************************************/

          vector<double> ptsx ;
          vector<double> ptsy ;

          for (size_t i = counter_thing; i < counter_thing + 50; ++i) {
            ptsx.push_back(map_waypoints_x_high_res[i]);
            ptsy.push_back(map_waypoints_y_high_res[i]);
          

          }

          counter_thing += 12;


          double px = j[1]["x"];
          double py = j[1]["y"];
          double psi = 0    ;  
          double v = j[1]["speed"];

          // double steer_value = j[1]["steering_angle"] ;

          /****************************************
           * 3. Convert map space to car space 
           ****************************************/

          // Use Eigen vector as polyfit() requires it
          Eigen::VectorXd   x_car_space = Eigen::VectorXd( ptsx.size() ) ;
          Eigen::VectorXd   y_car_space = Eigen::VectorXd( ptsx.size() ) ;

          for (int i = 0;   i < ptsx.size() ;   i++) {
            x_car_space(i) = ptsx[i] ;
            y_car_space(i) = ptsy[i] ;
            //cout << "ptsx[i] " << ptsx[i] << "\tpx " << px << "\tcos(psi) " 
            //<< cos(psi) << "\tsin(psi) " << sin(psi) << endl ;
          }

          /****************************************
           * 4. Fit line to get coefficients
           ****************************************/

          // cout << "xvals" << x_car_space << endl ;
          // cout << "yvals" << y_car_space << endl ;

          auto coeffs = polyfit(x_car_space, y_car_space, 3) ;
          // cout << "coeffs\t" << coeffs << endl ;

           /****************************************
           * 5. Error calculation (Cross track and Psi) and state definition.
           ****************************************/
          
          Eigen::VectorXd state(6) ;
        
          // where * latency is used to allow for latency in state calculation
          const double latency = .08 ;
          // cout << "px original " << px << endl ;

          // convert to m/s 
          v = v * 0.44704;

          // px = v * latency ;
          // cout << "px transformed" << px << endl ;

          const double Lf = 2.9;
          // cout << "psi original" << psi << endl ;
          // psi = - v * psi / Lf * latency ;
          // cout << "psi transformed" << psi << endl ;

          double cte  = polyeval(coeffs, px) ;
          // double epsi = atan( coeffs[1] ) ;

          // using derivative at px 
          double epsi = - atan( coeffs[1] +
                              (2 * coeffs[2] * px) +
                              (3 * coeffs[3] * (px * px) ) );

          state << px, py, psi, v, cte, epsi ;
          // cout << "state " << psi << endl ;

          /****************************************
           * 6. Solve (Computational Infastructure for Operations Research library)
           ****************************************/

          cout << "Solving" << endl ;

          auto vars = mpc.Solve(state, coeffs) ;

          // Convert from radians to normalized range.
          // steer_value           = steer_value / deg2rad(25) ;
          auto steer_value           = - mpc.steering_angle ;
          double throttle_value = mpc.throttle ;

          /****************************************
           * 7. Pass output to simulator
           ****************************************/

          json msgJson;
          //msgJson["steering_angle"] =   steer_value;
          //msgJson["throttle"]       =   throttle_value;

          /****************************************
           * 8. Predicted line visual for simulator
           ****************************************/

          // the points in the simulator are connected by a Green line
          // points are in reference to the vehicle's coordinate system
          
          vector<double> mpc_x_vals;
          vector<double> mpc_y_vals;

          int x_car_space_len = x_car_space.size() ;
          for (int i = 6; i < vars.size(); i ++) {
            if (i % 2 == 0 ){ 
              
              mpc_x_vals.push_back( vars[i] ) ;

            } else {
              
              mpc_y_vals.push_back( vars[i] ) ;
            }
          }

          //msgJson["mpc_x"] = mpc_x_vals;
          //msgJson["mpc_y"] = mpc_y_vals;


          /****************************************
           * 9. Way point visual for simulator
           ****************************************/

          // the points in the simulator are connected by a Yellow line

          vector<double> next_x_vals;
          vector<double> next_y_vals;

          for (int i = 1;  i < x_car_space.size();  i++) {
            next_x_vals.push_back(x_car_space[i] ) ;
            next_y_vals.push_back(y_car_space[i] ) ;
          }

          msgJson["next_x"] = mpc_x_vals;
          msgJson["next_y"] = mpc_y_vals;


          auto msg = "42[\"control\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;

          /****************************************
           * 10. Add latency to mimic real world driving conditions
           ****************************************/

          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //

          // TODO look at chrono::high_resolution_clock() 
          //this_thread::sleep_for(chrono::milliseconds(10));
          (*ws).send(msg.data(), msg.length(), uWS::OpCode::TEXT);

        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
       (*ws).send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> *ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> *ws, int code,
                         char *message, size_t length) {
    (*ws).close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen("0.0.0.0", port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
