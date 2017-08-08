#include "path.h"
#include "classifier.h"
#include "Eigen-3.3/Eigen/Dense"
#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <random>
#define _USE_MATH_DEFINES
#include <math.h>
#include "spline.h"
constexpr double pi() { return M_PI; }
#include "behavior_planner.h"
#include <algorithm>

path::path() {}
path::~path() {}

Behavior *behavior = new Behavior;

Vehicle *r_daneel_olivaw = new Vehicle;  // our self driving car
GNB		*classifier = new GNB;
Vehicle *target = new Vehicle;
path		*our_path = new path;

map<int, Vehicle>				other_vehicles;
vector < path::Weighted_costs > weighted_costs;
default_random_engine			generator;

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;

double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

void path::init() {

	behavior->init();

	//vector< vector<double> > X_train = classifier->load_state("./train_states.txt");
	//vector< string > Y_train = classifier->load_label("./train_labels.txt");
	//classifier->train(X_train, Y_train);

	our_path->previous_path_keeps = 5;  // starting point!
	our_path->timestep = .02;
	our_path->T = 2;
	our_path->trajectory_samples = 5;
	our_path->distance_goal = our_path->T * 8;
	our_path->SIGMA_S = { 1., .01, .001 };
	our_path->SIGMA_D = { .05, .01, .001 };
	our_path->current_lane_target = 3.8;

}

void path::sensor_fusion_predict_and_behavior(vector< vector<double>> sensor_fusion, long long time_difference_b) {
	// Store raw sensor_fusion observations and make a prediction 

	// 1. Update vehicles list
	for (size_t i = 0; i < sensor_fusion.size(); ++i) {

		int id = sensor_fusion[i][0];
		if (other_vehicles.find(id) == other_vehicles.end()) {
			// if vehicle doesn't exist create a new one & init
			Vehicle *vehicle = new Vehicle;
			vehicle->update_sensor_fusion(sensor_fusion, i, time_difference_b) ;
			other_vehicles.insert(make_pair(id, *vehicle));
		}

		// 2. Update previous sensor readings
		Vehicle *vehicle = &other_vehicles.find(id)->second;
		vehicle->update_sensor_fusion_previous();

		// 3. Update new sensor readings
		vehicle->update_sensor_fusion(sensor_fusion, i, time_difference_b);

		// 4. Run classifier for prediction
		//vehicle->predicted_state = classifier->predict(vehicle->D[1]);
	}

	if (our_path->last_trajectory.size() != 0) {
		auto lane = behavior->update_behavior_state(our_path->last_trajectory, our_path);
		our_path->current_lane_target = lane.d;
	}

}


void path::update_our_car_state(path::MAP *MAP, double car_x, double car_y, double car_s, double car_d,
	double car_yaw, double car_speed, long long time_difference) {
	
	// previous
	r_daneel_olivaw->S_p = r_daneel_olivaw->S;
	r_daneel_olivaw->D_p = r_daneel_olivaw->D;

	// new readings
	//cout << chrono::high_resolution_clock::to_time_t(chrono::high_resolution_clock::now()) << endl;
	

	auto dt = time_difference;

	r_daneel_olivaw->S[0] = car_s;
	r_daneel_olivaw->D[0] = car_d;
	//cout << "r_daneel_olivaw->S[0] \t " << r_daneel_olivaw->S[0] << endl;
	//cout << "r_daneel_olivaw->S[1] \t "  << r_daneel_olivaw->S[1] << endl;
	//cout << "r_daneel_olivaw->S[2] \t \n" << r_daneel_olivaw->S[2] << endl;
	//cout << "r_daneel_olivaw->D[0] \t " << r_daneel_olivaw->D[0] << endl;
	//cout << "r_daneel_olivaw->D[1] \t " << r_daneel_olivaw->D[1] << endl;
	//cout << "r_daneel_olivaw->D[2] \t " << r_daneel_olivaw->D[2] << endl;

	r_daneel_olivaw->S[1] = (r_daneel_olivaw->S[0] - r_daneel_olivaw->S_p[0]) / dt;
	r_daneel_olivaw->D[1] = (r_daneel_olivaw->D[0] - r_daneel_olivaw->D_p[0]) / dt;
	
	if ( r_daneel_olivaw->S_p[1] != 0) {
		r_daneel_olivaw->S[2] = (r_daneel_olivaw->S[1] - r_daneel_olivaw->S_p[1]) / dt;  // ie 100 - 90 = change of 10
		r_daneel_olivaw->D[2] = (r_daneel_olivaw->D[1] - r_daneel_olivaw->D_p[1]) / dt;
	}

	r_daneel_olivaw->x = car_x;
	r_daneel_olivaw->y = car_y;
	r_daneel_olivaw->yaw = car_yaw;
	r_daneel_olivaw->speed = car_speed;	 

	
	target->S[0] = car_s + our_path->distance_goal;

	if (car_speed < 30) {
		target->S[0] = car_s + our_path->T * 7;
		target->S[1] = 5;
		target->S[2] = .01;
	}

	if (car_speed < 45 && car_speed > 30) {
		target->S[0] = car_s + our_path->T * 7;
		target->S[1] = 5;
		target->S[2] = 0.001;
	}

	if (car_speed > 45) {
		target->S[0] = car_s + our_path->T * 4;
		target->S[1] = .1; //  velocity
		target->S[2] = 0.00000;  // 
	}
	
	if (our_path->last_trajectory.size() != 0) {
		target->D[0] = our_path->current_lane_target;
		cout << "target->D[0]" << target->D[0] << endl;

		target->D[1] = .001;   // SHOULD BE SMALL
		target->D[2] = .003;
	}
	else {
		target->D[0] = our_path->current_lane_target;
		target->D[1] = .01;  
		target->D[2] = 0.01;
	}

	if (our_path->last_trajectory.size() != 0) {

		// this should be in behavior planner?
		if (our_path->collision_cost(our_path->last_trajectory) == 1) {
			cout << "Slowing down for car" << endl;

			target->S[0] = car_s + our_path->T * 3;
			target->S[1] = 2;
			target->S[2] = -.0005;
		}

		if (our_path->speed_limit_cost(our_path->last_trajectory) > .4) {
			target->S[0] = car_s + our_path->T * 3;
			target->S[1] = 1;  //  velocity
			target->S[2] = 0.00000;  // 
		}
	}
	
	//cout << "target->D[0]" << target->D[0] << endl;
	target->update_target_state(0);
	//cout << "target->D[0]" << target->D[0] << endl;
}


vector<double> path::trajectory_generation() {
	/****************************************
	* find best trajectory according to weighted cost function
	****************************************/

	// 1. Generate random nearby goals
	vector< vector<double>> goals;


	// first goal
	goals.resize(1);
	goals[0].insert(end(goals[0]), begin(target->S_TARGETS), end(target->S_TARGETS));
	goals[0].insert(end(goals[0]), begin(target->D_TARGETS), end(target->D_TARGETS));

	double t = our_path->T - our_path->timestep;
	double b = our_path->T + our_path->timestep;;
	goals[0].push_back(t);

	// other goals
	while (t <= b) {

		target->update_target_state(t);
		vector< vector<double>> target_goals;
		vector<double> new_goal;

		//cout << "target->D[0]" << target->D[0] << endl;

		target_goals.resize(our_path->trajectory_samples);

		for (size_t i = 0; i < target_goals.size(); ++i) {
			target_goals[i] = wiggle_goal(t);
		}

		goals.insert(end(goals), begin(target_goals), end(target_goals));
		t += our_path->timestep;
	}

	// 2. Store jerk minimal trajectories for all goals
	vector< vector<double>> trajectories;
	trajectories.resize(goals.size());

	vector<double> s_goal, d_goal, s_coeffecients, d_coeffecients,
		start_s, start_d;
	double t_2;
	start_s = { r_daneel_olivaw->S[0], r_daneel_olivaw->S[1], r_daneel_olivaw->S[2] };
	start_d = { r_daneel_olivaw->D[0], r_daneel_olivaw->D[1], r_daneel_olivaw->D[2] };

	for (size_t i = 0; i < goals.size(); ++i) {

		s_goal = { goals[i][0], goals[i][1], goals[i][2] };
		d_goal = { goals[i][3], goals[i][4], goals[i][5] };
		t_2 = goals[i][6];

		s_coeffecients = jerk_minimal_trajectory(start_s, s_goal, t_2);
		d_coeffecients = jerk_minimal_trajectory(start_d, d_goal, t_2);

		trajectories[i].insert(end(trajectories[i]), begin(s_coeffecients), end(s_coeffecients));
		trajectories[i].insert(end(trajectories[i]), begin(d_coeffecients), end(d_coeffecients));
		trajectories[i].push_back(t);
	}

	// 3. Find best using weighted cost function
	double min_cost = 1e10;
	double cost;
	vector<double> best_trajectory;
	for (size_t i = 0; i < trajectories.size(); ++i) {

		cost = calculate_cost(trajectories[i]);
		if (cost < min_cost) {
			min_cost = cost;
			best_trajectory = trajectories[i];
		}
		
	}

	cout << "Best trajectory cost: " << cost << endl;


	our_path->last_trajectory = best_trajectory;
	our_path->last_n_trajectories.resize(our_path->last_n_trajectories.size() + 1);
	our_path->last_n_trajectories[our_path->last_n_trajectories.size() - 1].insert(end(our_path->last_n_trajectories[our_path->last_n_trajectories.size() -1]),
		begin(best_trajectory), end(best_trajectory));
	
	return best_trajectory;

}

vector<double> path::wiggle_goal(double t) {

	vector<double> new_goal(7);
	for (size_t i = 0; i < 3; ++i) {

		normal_distribution<double> distribution(target->S_TARGETS[i], our_path->SIGMA_S[i]);
		new_goal[i] = distribution(generator);
	}
	for (size_t i = 0; i < 3; ++i) {

		normal_distribution<double> distribution2(target->D_TARGETS[i], our_path->SIGMA_D[i]);
		new_goal[i + 3] = distribution2(generator);
	}

	//cout << "new goal s\t" << new_goal[0] << " \tnew goal d\t" << new_goal[3] << endl;
	new_goal[6] = t;
	return new_goal;

}

path::Previous_path path::merge_previous_path(path::MAP *MAP, vector< double> previous_path_x,
	vector< double> previous_path_y, double car_yaw, double car_s, double car_d, double end_path_s, double end_path_d) {

	path::Previous_path Previous_path;
	int p_x_size;
	p_x_size = previous_path_x.size();

	
	if (p_x_size != 0) {

		our_path->previous_path_keeps = max(1, 99 - p_x_size); 
		//our_path->previous_path_keeps = 25;
		cout << "our_path->previous_path_keeps " << our_path->previous_path_keeps << endl;

		//cout << "car yaw " << car_yaw << endl;

		//car_yaw = 4;
		//cout << "car yaw " << car_yaw << endl;

		//steps_moved = time_difference

		vector<double> new_s_d = getFrenet(previous_path_x[our_path->previous_path_keeps ], previous_path_y[our_path->previous_path_keeps], car_yaw, MAP->waypoints_x_upsampled, MAP->waypoints_y_upsampled);

		Previous_path.s = new_s_d[0];
		Previous_path.d = new_s_d[1];

		/*
		for (size_t i = 0; i < our_path->previous_path_keeps + 1 ; i++) {
			Previous_path.X.push_back(previous_path_x[i]);
			Previous_path.Y.push_back(previous_path_y[i]);
		}
		*/

		int x_size = Previous_path.X.size();
	}
	else {  // Init case
		Previous_path.s = car_s;
		Previous_path.d = car_d;
	}
	return Previous_path;

}

path::X_Y path::convert_new_path_to_X_Y_and_merge(path::MAP* MAP, path::S_D S_D_, path::Previous_path Previous_path) {

	path::X_Y X_Y;
	//X_Y.X = Previous_path.X;
	//X_Y.Y = Previous_path.Y;

	for (size_t i = 0; i < S_D_.S.size(); ++i) {

		vector<double> a = getXY(S_D_.S[i], S_D_.D[i], MAP->waypoints_s_upsampled,
			MAP->waypoints_x_upsampled, MAP->waypoints_y_upsampled);
		X_Y.X.push_back(a[0]);
		X_Y.Y.push_back(a[1]);
		
	}

	cout << "\nX_Y.X 0 \t " << X_Y.X[0] << "\t X_Y.Y 0 \t" << X_Y.Y[0] << endl;
	cout << "X_Y.X 1 \t " << X_Y.X[1] << "\t X_Y.Y 1 \t" << X_Y.Y[1] << endl;
	//cout << "X_Y.X 2 \t " << X_Y.X[2] << "\t X_Y.Y 0 \t" << X_Y.Y[2] << endl;
	//cout << "X_Y.X 50 \t " << X_Y.X[50] << "\t X_Y.Y 50 \t" << X_Y.Y[50] << endl;
	//cout << "X_Y.X " << S_D_.S.size()-1 << "\t " << X_Y.X[S_D_.S.size()-1] << "\t X_Y.Y \t" << X_Y.Y[S_D_.S.size()-1] << endl;

	/*	
	double x_mean, y_mean;
	x_mean = 0;
	y_mean = 0;
	int x_size = our_path->previous_path_keeps;
	int merge_size = 9;
	for (size_t i = x_size - merge_size; i < x_size + merge_size; ++i) {
		x_mean += X_Y.X[i];
		y_mean += X_Y.Y[i];
	}
	x_mean = x_mean / (merge_size * 2);
	y_mean = y_mean / (merge_size * 2);
	for (size_t i = x_size - 3; i < x_size + 3; ++i) {
		X_Y.X[i] = X_Y.X[i] * .97 + x_mean * .03;
		X_Y.Y[i] = X_Y.Y[i] * .97 + y_mean * .03;
	}
	for (size_t i = x_size - 6; i < x_size + 6; ++i) {
		X_Y.X[i] = X_Y.X[i] * .99 + x_mean * .01;
		X_Y.Y[i] = X_Y.Y[i] * .99 + y_mean * .01;
	}
	for (size_t i = x_size - 9; i < x_size + 9; ++i) {
		X_Y.X[i] = X_Y.X[i] * .999 + x_mean * .001;
		X_Y.Y[i] = X_Y.Y[i] * .999 + y_mean * .001;
	}
	for (size_t i = x_size - 12; i < x_size + 12; ++i) {
		X_Y.X[i] = X_Y.X[i] * .9999 + x_mean * .0001;
		X_Y.Y[i] = X_Y.Y[i] * .9999 + y_mean * .0001;
	}
	*/
	
	// exp(rate)
	
	our_path->previous_path_size = X_Y.X.size();
	
	return X_Y;
}


/****************************************
* Cost functions
****************************************/

double path::calculate_cost(vector<double> trajectory) {
	double cost = 0;

	cost += 1 * collision_cost(trajectory);
	cost += 1 * total_acceleration_cost(trajectory);
	cost += 1 * max_acceleration_cost(trajectory);
	cost += 1 * efficiency_cost(trajectory);
	cost += 1 * total_jerk_cost(trajectory);
	cost += 1 * buffer_cost(trajectory);
	cost += 1 * s_diff_cost(trajectory);
	cost += 1 * d_diff_cost(trajectory);
	cost += 1 * speed_limit_cost(trajectory);
	cost += 1 * max_jerk_cost(trajectory);
	//cost += 1 * stay_in_lane(trajectory);

	return cost;
}

double path::buffer_cost(vector<double> trajectory) {

	double nearest = nearest_approach_to_any_vehicle(trajectory);
	double cost = logistic(2 * r_daneel_olivaw->radius / nearest);
	return cost;

}

double path::s_diff_cost(vector<double> trajectory) {

	vector<double> S, S_coefficients; // TODO better way to do this
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };
	double T = trajectory[12];
	double cost = 0;
	double difference;

	target->update_target_state(T);
	S_coefficients = get_ceoef_and_rates_of_change(S);
	for (size_t i = 0; i < S_coefficients.size() - 1; ++i) {

		// actual - expected
		difference = fabs(S_coefficients[i] - target->S_TARGETS[i]);
		cost += logistic(difference / our_path->SIGMA_S[i]);
	}

	return cost;

}

vector<double> path::get_ceoef_and_rates_of_change(vector<double> coefficients) {

	vector<double> coefficients_d, out;
	double a;

	out.push_back(coefficients_to_time_function(coefficients, 2));
	coefficients_d = differentiate_polynomial(coefficients);

	for (size_t i = 0; i < 3; ++i) {

		a = coefficients_to_time_function(coefficients_d, 2);
		out.push_back(a);
	}
	return out;
}

double path::d_diff_cost(vector<double> trajectory) {

	vector<double> D, D_coefficients; // TODO better way to do this
	D = { trajectory[6], trajectory[7], trajectory[8], trajectory[9], trajectory[10], trajectory[11] };
	double T = trajectory[12];
	double cost = 0;
	double difference;

	target->update_target_state(T);
	D_coefficients = get_ceoef_and_rates_of_change(D);
	for (size_t i = 0; i < D_coefficients.size() - 1; ++i) {

		// actual - expected
		difference = fabs(D_coefficients[i] - target->D_TARGETS[i]);
		cost += logistic(difference / our_path->SIGMA_D[i]);
	}

	return cost;

}

double path::total_jerk_cost(vector<double> trajectory) {

	vector<double> S, S_dot_coefficients, S_dot_dot_coeffecients, jerk;
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };

	double T = trajectory[12];
	double cost = 0;
	double total_jerk = 0;
	double expected_jerk_1_second = .1;

	S_dot_coefficients = differentiate_polynomial(S);
	S_dot_dot_coeffecients = differentiate_polynomial(S_dot_coefficients);
	jerk = differentiate_polynomial(S_dot_dot_coeffecients);

	double delta_time = T / 10;

	for (size_t i = 0; i < 10; ++i) {

		auto time = delta_time * i;
		auto acceleration = coefficients_to_time_function(jerk, time);
		total_jerk += fabs(acceleration * delta_time);
	}


	auto accerlation_per_second = total_jerk / T;
	cost = logistic(accerlation_per_second / expected_jerk_1_second);

	return cost;
}

double path::max_jerk_cost(vector<double> trajectory) {

	vector<double> S, S_dot_coefficients, S_dot_dot_coeffecients, jerk, all_jerks;
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };

	double T = trajectory[12];
	double max_jerk = 1;

	S_dot_coefficients = differentiate_polynomial(S);
	S_dot_dot_coeffecients = differentiate_polynomial(S_dot_coefficients);
	jerk = differentiate_polynomial(S_dot_dot_coeffecients);

	double delta_time = T / 10;

	for (size_t i = 0; i < 10; ++i) {

		auto time = delta_time * i;
		all_jerks.push_back(coefficients_to_time_function(S_dot_dot_coeffecients, time));
	}

	bool flag = false;
	for (size_t i = 0; i < 10; ++i) {

		if (fabs(all_jerks[i]) > max_jerk) { flag = true; }
	}

	if (flag == true) {
		return 1;
	}
	else {
		return 0;
	}
}

double path::max_acceleration_cost(vector<double> trajectory) {

	vector<double> S, S_dot_coefficients, S_dot_dot_coeffecients, all_accelerations;
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };

	double t_ = trajectory[12];
	double cost = 0;
	double total_acceleration = 0;
	double max_acceleration = 10;

	S_dot_coefficients = differentiate_polynomial(S);
	S_dot_dot_coeffecients = differentiate_polynomial(S_dot_coefficients);
	double delta_time = t_ / 10;  // out path T / increment

	for (size_t i = 0; i < 10; ++i) {

		auto time = delta_time * i;
		all_accelerations.push_back(coefficients_to_time_function(S_dot_dot_coeffecients, time));
	}

	bool flag = false;
	for (size_t i = 0; i < 10; ++i) {

		if (fabs(all_accelerations[i]) > max_acceleration) { flag = true; }
	}

	if (flag == true) {
		return 1;
	}
	else {
		return 0;
	}

}

double path::total_acceleration_cost(vector<double> trajectory) {

	vector<double> S, S_dot_coefficients, S_dot_dot_coeffecients;
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };

	const double T_ = trajectory[12];
	double cost = 0;
	double total_acceleration = 0;
	const double expected_acceleration_time = 5;  // wouldn't this be 3 seconds if T_ = 3

	S_dot_coefficients = differentiate_polynomial(S);
	S_dot_dot_coeffecients = differentiate_polynomial(S_dot_coefficients);
	double delta_time = T_ / 100;

	for (size_t i = 0; i < 100; ++i) {

		auto time = delta_time * i;
		auto acceleration = coefficients_to_time_function(S_dot_dot_coeffecients, time);
		total_acceleration += fabs(acceleration * delta_time);
	}
	
	auto accerlation_per_second = total_acceleration / T_;	
	cost = logistic(accerlation_per_second / expected_acceleration_time);

	return cost;

}

double path::speed_limit_cost(vector<double> trajectory) {
	vector<double> S;
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };
	const double T_ = trajectory[12];

	double delta_time = T_ / (our_path->T / our_path->timestep);
	double max_speed = 48;

	auto S_dot_coefficients = differentiate_polynomial(S);

	for (size_t i = 0; i < (our_path->T / our_path->timestep); ++i) {

		auto time = delta_time * i;
		auto velocity = coefficients_to_time_function(S_dot_coefficients, time);
		
		if (velocity > max_speed) { return 1; }
	}
	return 0;

}


double path::collision_cost(vector<double> trajectory) {

	double a = nearest_approach_to_any_vehicle(trajectory);

	double b = 2 * r_daneel_olivaw->radius;
	//cout << b << endl;
	if (a < b) { 
		// cout << a << endl; 
		return 1.0; 
	}
	else { return 0.0; }
}


double path::stay_in_lane(vector<double> trajectory){
	vector<double> D;
	D = { trajectory[6], trajectory[7], trajectory[8], trajectory[9], trajectory[10], trajectory[11] };
	const double T_ = trajectory[12];
	const double delta_time = T_ / (our_path->T / our_path->timestep);

	double lane_cost = 0;
	double d_goal = trajectory[6];
	
	for (size_t i = 0; i < (our_path->T / our_path->timestep); ++i) {

		auto time = delta_time * i;
		auto co_e = coefficients_to_time_function(D, time);
		if (abs(d_goal - co_e) > 1) {
			lane_cost += .01;
		}	
	
	}
	auto out = logistic( lane_cost );
	// cout << out << endl;
	return out;
}


double path::nearest_approach_to_any_vehicle(vector<double> trajectory) {
	// returns closest distance to any vehicle

	double a = 1e9;
	double b;
	for (size_t i = 0; i < other_vehicles.size(); ++i) {
		b = r_daneel_olivaw->nearest_approach(trajectory, other_vehicles[i]);
		if (b < a) { a = b; }
	}
	return a;

}

double Vehicle::nearest_approach(vector<double> trajectory, Vehicle vehicle) {

	double T, s_time, d_time, a, b, c, e, t;
	a = 1e9;
	T = trajectory[12];

	vector<double> S, D; // TODO better way to do this
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };
	D = { trajectory[6], trajectory[7], trajectory[8], trajectory[9], trajectory[10], trajectory[11] };

	for (size_t i = 0; i < (our_path->T / our_path->timestep); ++i) {
		t = double(i) / (our_path->T / our_path->timestep) * T;
		s_time = coefficients_to_time_function(S, t);
		d_time = coefficients_to_time_function(D, t);

		vehicle.update_target_state(t);

		b = pow((s_time - vehicle.s_target), 2);
		c = pow((d_time - vehicle.d_target), 2);
		e = sqrt(b + c);

		if (e < a) { a = e; }
	}
	return a;
}

path::S_D  path::build_trajectory(vector<double> trajectory, long long build_trajectory_time) {

	vector<double> S, D, X, Y; // TODO better way to do this
							   // refactor using S_D struct
	
	// Average previously trajectory

	/*
	auto super_time = 0;
	auto sums_time = 0;
	auto factor = min(int(our_path->last_n_trajectories.size()),1);
	cout << "factor" << factor << endl;
	vector<double> sums_S, sums_D;

	sums_S.resize(6);
	sums_D.resize(6);

	
	for (size_t i = our_path->last_n_trajectories.size() - factor; i < our_path->last_n_trajectories.size(); ++i) {
		for (size_t j = 0; j < 6; ++j) {
			sums_S[j] += our_path->last_n_trajectories[i][j];
			sums_D[j] += our_path->last_n_trajectories[i][j + 6];
		}
		sums_time += trajectory[12];
	}


	for (size_t i = 0; i < 6; ++i) {
		S.push_back((trajectory[i] * 3 + sums_S[i]) / (factor + 3));
		D.push_back((trajectory[i + 6] * 3 + sums_D[i]) / (factor + 3));
		super_time = (trajectory[12] * 3 + sums_time) / (factor + 3);

		cout << "S[i] \t" << S[i] << " \t D[i] \t" << D[i] << endl;
	}
	*/
	
	S_D S_D_;
	for (size_t i = 0; i < 6; ++i) {
		S.push_back(trajectory[i]);
		D.push_back(trajectory[6+ i]);
		//cout << "S[i] \t" << S[i] << " \t D[i] \t" << D[i] << endl;
	}
	auto super_time = (trajectory[12]);

	double time = (build_trajectory_time ) * .002;; // go back in time like pitbull
	while (time <= super_time) {
		S_D_.S.push_back(coefficients_to_time_function(S, time));
		S_D_.D.push_back(coefficients_to_time_function(D, time));
		time += our_path->timestep;
	}
	return S_D_;

}

double path::efficiency_cost(vector<double> trajectory) {
	
	vector<double> S;
	S = { trajectory[0], trajectory[1], trajectory[2], trajectory[3], trajectory[4], trajectory[5] };
	double T_ = trajectory[12];

	auto average_velocity = coefficients_to_time_function(S, T_) / T_;
	target->update_target_state(T_);
	auto target_s = target->S_TARGETS[0];
	auto target_velocity = target_s / T_;

	return logistic(2 * (target_velocity - average_velocity) / average_velocity);

}

vector<double> path::differentiate_polynomial(vector<double> coefficients) {
	// given a vector of coefficients, returns 
	vector<double> out;
	int next_degree;
	double result;

	for (size_t i = 1; i < coefficients.size(); ++i) {
		next_degree = i + 1;
		result = next_degree * coefficients[i];
		out.push_back(result);
	}
	return out;
}

double path::logistic(double x) {
	return 2.0 / (1 + exp(-x)) - 1.0;
}

double path::coefficients_to_time_function(vector<double> coefficients, double t) {
	// Returns a function of time given coefficients
	double total = 0.0;
	for (size_t i = 0; i < coefficients.size(); ++i) {
		total += coefficients[i] * pow(t, i);
	}
	return total;
}

vector<double> path::jerk_minimal_trajectory(vector< double> start, vector <double> end, double T)
{
	/*
	Calculate the Jerk Minimizing Trajectory that connects the initial state
	to the final state in time T.

	INPUTS

	start - the vehicles start location given as a length three array
	corresponding to initial values of [s, s_dot, s_double_dot]

	end   - the desired end state for vehicle. Like "start" this is a
	length three array.

	T     - The duration, in seconds, over which this maneuver should occur.

	OUTPUT
	an array of length 6, each value corresponding to a coefficent in the polynomial
	s(t) = a_0 + a_1 * t + a_2 * t**2 + a_3 * t**3 + a_4 * t**4 + a_5 * t**5

	EXAMPLE

	> JMT( [0, 10, 0], [10, 10, 0], 1)
	[0.0, 10.0, 0.0, 0.0, 0.0, 0.0]
	*/

	const double s_i = start[0];
	const double s_i_dot = start[1];
	const double s_i_dot_dot = start[2] / 2;

	const double s_f = end[0];
	const double s_f_dot = end[1];
	const double s_f_dot_dot = end[2];

	const double s_r_1 = s_f - (s_i + s_i_dot * T + (s_i_dot_dot * T * T) / 2);
	const double s_r_2 = s_f_dot - (s_i_dot + s_i_dot_dot * T);
	const double s_r_3 = s_f_dot_dot - s_i_dot_dot;

	VectorXd t_1(3), t_2(3), t_3(3);
	t_1 << pow(T, 3), pow(T, 4), pow(T, 5);
	t_2 << 3 * pow(T, 2), 4 * pow(T, 3), 5 * pow(T, 4);
	t_3 << 6 * pow(T, 1), 12 * pow(T, 2), 20 * pow(T, 3);

	MatrixXd s_matrix(3, 1), T_matrix(3, 3);
	s_matrix << s_r_1, s_r_2, s_r_3;
	T_matrix.row(0) = t_1;
	T_matrix.row(1) = t_2;
	T_matrix.row(2) = t_3;

	MatrixXd T_inverse = T_matrix.inverse();

	MatrixXd output(3, 1);
	output = T_inverse * s_matrix;

	double a_3 = double(output.data()[0]);
	double a_4 = double(output.data()[1]);
	double a_5 = double(output.data()[2]);

	vector<double> results;
	results = { s_i, s_i_dot, s_i_dot_dot, a_3, a_4, a_5 };

	// cout << output << endl;

	return results;

}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> path::getFrenet(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{
	int next_wp = NextWaypoint(x, y, theta, maps_x, maps_y);

	int prev_wp;
	prev_wp = next_wp - 1;
	if (next_wp == 0)
	{
		prev_wp = maps_x.size() - 1;
	}

	double n_x = maps_x[next_wp] - maps_x[prev_wp];
	double n_y = maps_y[next_wp] - maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x + x_y*n_y) / (n_x*n_x + n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x, x_y, proj_x, proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000 - maps_x[prev_wp];
	double center_y = 2000 - maps_y[prev_wp];
	double centerToPos = distance(center_x, center_y, x_x, x_y);
	double centerToRef = distance(center_x, center_y, proj_x, proj_y);

	if (centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for (int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i], maps_y[i], maps_x[i + 1], maps_y[i + 1]);
	}

	frenet_s += distance(0, 0, proj_x, proj_y);

	return { frenet_s,frenet_d };

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> path::getXY(double s, double d, vector<double> maps_s, vector<double> maps_x, vector<double> maps_y)
{
	int prev_wp = -1;

	while (s > maps_s[prev_wp + 1] && (prev_wp < (int)(maps_s.size() - 1)))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp + 1) % maps_x.size();

	double heading = atan2((maps_y[wp2] - maps_y[prev_wp]), (maps_x[wp2] - maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s - maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp] + seg_s*cos(heading);
	double seg_y = maps_y[prev_wp] + seg_s*sin(heading);

	double perp_heading = heading - pi() / 2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return { x,y };

}

double path::distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));
}
int path::ClosestWaypoint(double x, double y, vector<double> maps_x, vector<double> maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for (int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x, y, map_x, map_y);
		if (dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}
	}

	return closestWaypoint;
}
int path::NextWaypoint(double x, double y, double theta, vector<double> maps_x, vector<double> maps_y)
{

	int closestWaypoint = ClosestWaypoint(x, y, maps_x, maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y - y), (map_x - x));

	double angle = abs(theta - heading);

	if (angle > pi() / 4)
	{
		closestWaypoint++;
	}

	return closestWaypoint;

}
