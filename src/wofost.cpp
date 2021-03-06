/*
Authors: Robert Hijmans and Huang Fang
Date: June 2016

License: GNU General Public License (GNU GPL) v. 2
*/

#include <vector>
#include "wofost.h"
#include "SimUtil.h"
#include <math.h>
#include <string.h>
//#include <iostream>
//#include <Rcpp.h>


void WofostModel::weather_step() {
	if (time >= wth.tmin.size()) {
		fatalError = true;
	} else {
		atm.TMIN = wth.tmin[time];
		atm.TMAX = wth.tmax[time];
		atm.TEMP  = (atm.TMIN + atm.TMAX) / 2.;
		atm.DTEMP = (atm.TMAX + atm.TEMP) / 2.;

		atm.AVRAD = wth.srad[time] * 1000;
		atm.WIND = wth.wind[time];
		atm.VAP = wth.vapr[time] * 10;
		atm.RAIN = wth.prec[time] / 10 ; // cm !

		DOY = doy_from_days(wth.date[time]);

		ASTRO();
		std::vector<double> penman = PENMAN(DOY, atm.latitude, atm.elevation, atm.ANGSTA, atm.ANGSTB, atm.TMIN, atm.TMAX, atm.AVRAD, atm.VAP, atm.WIND, atm.ATMTR);

		atm.E0 = penman[0];
		atm.ES0 = penman[1];
		atm.ET0 = penman[2];
	}
}


void WofostModel::model_output(){
    //out.push_back( { double(step), crop.TSUM, crop.DVS, crop.GASS, crop.LAI, crop.WLV, crop.WST, crop.WRT, crop.WSO,
		//	atm.E0, soil.SM, crop.TRA, soil.WLOW, soil.W, double(i)});

		out.push_back( {double(step), crop.TSUM, crop.DVS, crop.LAI, crop.WRT, crop.WLV, crop.WST, crop.WSO } );
}


void WofostModel::model_initialize() {

// start time (relative to weather data)
	if (control.modelstart < wth.date[0]) {
		std::string m = "model cannot start before beginning of the weather data";
	    messages.push_back(m);
	    fatalError = true;
	} else if (control.modelstart > wth.date[wth.date.size()-1]) {
		std::string m = "model cannot start after the end of the weather data";
	    messages.push_back(m);
	    fatalError = true;
	} else {
		time=0;
		while (wth.date[time] < control.modelstart) {
			time++;
		}
	}
//	Rcpp::Rcout << time << endl;

	if (control.ISTCHO == 0) { // model starts at emergence)
		ISTATE = 3;
	} else if (control.ISTCHO == 1) { // model starts at sowing
		ISTATE = 1;
	} else if (control.ISTCHO == 2) { // model starts prior to earliest pop.SSIble sowing date
		ISTATE = 0;
		STDAY_initialize();
	}

	DELT = 1.;
//	ISTATE = 3;

	control.IWB = control.IPRODL;
	if (control.IWB == 0) {
		IOX = 0;
	} else {
		IOX = control.IOXWL;   //for water-limited
	}

/*
	atm.latitude = latitude;
	atm.elevation = elevation;
	atm.AngstromA = ANGSTA;
	atm.AngstromB = ANGSTB;
	atm.CO2 = CO2;
*/


 //   DOY = wth.date[time].dayofyear();
	DOY = doy_from_days(wth.date[time]);

    crop.alive = true;
	fatalError = false;

	soil_initialize();
	if(control.npk_model){
		npk_soil_dynamics_initialize();
		npk_translocation_initialize();
		npk_demand_uptake_initialize();
	}

    crop.DVS = 0.;
    crop.WRT = 0.;
    crop.TADW = 0.;
    crop.WST = 0.;
    crop.WSO = 0.;
    crop.WLV = 0.;
    crop.LV[0] = 0.;
    crop.LASUM = 0.;
    crop.LAIEXP = 0.;
    crop.LAI = 0.;
    crop.RD = crop.p.RDI;
	crop.TSUM = 0;
	crop.TSUME = 0.;
	crop.DTSUME = 0.;
    crop.TRA = 0.;
	crop.GASS = 0.;

	// adjusting for CO2 effects
    double CO2AMAXadj = AFGEN2(crop.p.CO2AMAXTB, wth.CO2);
    double CO2EFFadj = AFGEN2(crop.p.CO2EFFTB, wth.CO2);
	double CO2TRAadj = AFGEN2(crop.p.CO2TRATB, wth.CO2);
	int n = crop.p.AMAXTB.size();
	for(int i=1; i<n; i=i+2) {
		crop.p.AMAXTB[i] = crop.p.AMAXTB[i] * CO2AMAXadj;
		crop.p.CO2EFFTB[i] = crop.p.CO2EFFTB[i] * CO2EFFadj;
		crop.p.CO2TRATB[i] = crop.p.CO2TRATB[i] * CO2TRAadj;
	}

	out_names = {"step", "Tsum", "DVS", "LAI", "WRT", "WLV", "WST", "WSO"};

}



void WofostModel::model_run() {

	step = 1;
	npk_step = 0;
	unsigned cropstart_step = step + control.cropstart;

	model_initialize();

// model can start long before crop and run the soil water balance
	bool crop_emerged = false;

	while (! crop_emerged) {

		weather_step();
		if(control.npk_model){
			npk_soil_dynamics_rates();
		} else{
			soil_rates();
		}
		soil.EVWMX = atm.E0;
		soil.EVSMX = atm.ES0;
		if (step >= cropstart_step) {
			if (ISTATE == 0 ) { 	// find day of sowing
				STDAY();
			} else if (ISTATE == 1) {	// find day of emergence
				crop.TSUME = crop.TSUME + crop.DTSUME * DELT;
				if (crop.TSUME >= crop.p.TSUMEM) {
					ISTATE = 3;
					crop_emerged = true;
				}
				crop.DTSUME = LIMIT(0., crop.p.TEFFMX - crop.p.TBASEM, atm.TEMP - crop.p.TBASEM);
			} else {
				crop_emerged = true;
			}
		}
		model_output(); //?
		if(control.npk_model){
			npk_soil_dynamics_states();
		} else {
			soil_states();
		}

		if (fatalError) {
			break;
		}
		time++;
		step++;
	}
	crop.emergence = step;
	// remove one step/day as crop simulation should start
	// on the day of emergence, not the next day
	time--;
	step--;
	out.pop_back();

	unsigned maxdur;
	if (control.IENCHO == 1) {
		maxdur = cropstart_step + control.IDAYEN;
	} else if (control.IENCHO == 2) {
		maxdur = step + control.IDURMX;
	} else if (control.IENCHO == 3) {
		maxdur = std::min(cropstart_step + control.IDAYEN, step + control.IDURMX);
	} else {
		// throw error
		maxdur = step + 365;
	}

	crop_initialize();

	while ((crop.alive) && (step < maxdur)) {

		weather_step();
		crop_rates();
		if(control.npk_model){
			npk_soil_dynamics_rates();
		} else {
			soil_rates();
		}
		model_output();
		crop_states();
		if(control.npk_model){
			npk_soil_dynamics_states();
		} else{
			soil_states();
		}
		time++;
		step++;

		if (fatalError) {
			break;
		}

	}
	if (control.IENCHO == 1) {
		// should continue until maxdur if water balance if IENCHO is 1
		while (step < maxdur) {
			weather_step();
			soil_rates();
			// assuming that the crop has been harvested..
			// not checked with fortran
			soil.EVWMX = atm.E0;
			soil.EVSMX = atm.ES0;
			model_output();
			crop_states();
			soil_states();
			time++;
			step++;
		}
	}
}


