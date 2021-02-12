#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <numeric>
#include <TFile.h>
#include <TTree.h>
#include <TH1.h>
#include <THStack.h>
#include <TApplication.h>
#include <TLegend.h>
#include <TCanvas.h>
#include <TPaveText.h>
#include <TPie.h>

using namespace std;

// ###################################################################################
// ###################################################################################
// Functions
// ###################################################################################
// ###################################################################################

// --------------------------------
// Levelization function
// --------------------------------
double Lev(double Val, double DiscountRate, double T0, double T1)
{
	return Val / pow(1 + DiscountRate, T1 - T0);
}
// --------------------------------
// Constant Payment for a loan
// --------------------------------
double PMT(double rate, double nper, double principle)
{
	double to_pay = principle * rate / (1 - pow(1 + rate, -nper));
	return to_pay;
}
// --------------------------------
// Build Cost Profile
// --------------------------------
vector <double> CapexProfile_Gaus(double occ, int nt, double constr_period)
{
	// Investment gaussian profile
	double sigma = constr_period / 3.;
	double T0    = - constr_period / 2.;
	double tot   = 0;

	vector <double> CapexProfile (nt, 0);
	for (int t(0); t < constr_period; t++)
	{
		double x = -constr_period + 0.5 + t;
		CapexProfile[t] = exp(-(x - T0) * (x - T0) / (2.* sigma * sigma));
		tot += CapexProfile[t];
	}
	// Normalization to unity in fabrication duration
	// [3] in Haeseleer
	// Overnight Construction Cost profile [year ref]
	// [4] in Haeseleer
	for (int t(0); t < constr_period; t++) CapexProfile[t] = CapexProfile[t] / tot * occ;

	return CapexProfile;
}

// ###################################################################################
// ###################################################################################
// Main program
// ###################################################################################
// ###################################################################################

int main(int argc, char* argv[])
{
	TApplication TApp ("app", &argc, argv);

// #################################################################################
// INPUT DATA
// #################################################################################

	// --------------------------------
	// Time steps
	// --------------------------------
	int NTimeSteps = 100;
	// --------------------------------
	// Financing Data
	// --------------------------------
	// Actualisation
	double DiscountRate_Operation = atof(argv[1]); //0.04; => Sensitivity
	double DiscountRate_LongTerm = 0.02;
	// Interest for IDC (interets intercalaires)
	double InterestRate_IDC = 0.08;
	// --------------------------------
	// Time Frame
	// --------------------------------
	// Investment
	int ConstructionPeriod = atof(argv[2]);//9; => Sensitivity
	// Operation
	int ProductionStart = ConstructionPeriod;
	int ProductionPeriod = 60;
	int ProductionStop = ProductionStart + ProductionPeriod;
	// Time at which an important maintenance is done
	int ProductionPeriodBeforeMaintenance = 40;
	// --------------------------------
	// Project size
	// --------------------------------
	// Nominal Energy
	double ElectricPower = 1.650e9; //[W]
	// Load Factor
	double LoadFactor = 0.90;
	// --------------------------------
	// Costs
	// --------------------------------
	// Reactor Overnight Cost - 3600 € / kW
	//double OCC = 3600 * ElectricPower / 1.e3;
	double OCC = atof(argv[3]) * 1e9; //5.e9; => Sensitivity
	// - Fix Costs - Provision for dismantling - 30% du OCC
	double DismantlingCost = 0.3 * OCC; //
	// - Fix Costs - Provision for important maintenance - 600 € / kW
	double MaintenanceCost = 600 * ElectricPower / 1.e3;
	// - Fix Costs - Staff, external consumption, Central functions, taxes - CdC 2014 : 120 €/kW
	double FixCost_OM = 110 * ElectricPower / 1e3;
	// - Var Costs - Fuel cost - CdC : 5.2 Euro / MWh
	double FuelCost = 5.2;
	// - Var Costs - Spent Fuel managment provisions - CdC : 4 Euro / MWh
	double SpentFuelProvisionCost = 3.8;

	// --------------------------------
	// Energy Production Profile
	// --------------------------------

	// Load Factor and energy production
	vector <double> LoadFactor_Profile (NTimeSteps, 0);
	vector <double> Energy_Profile (NTimeSteps, 0);

	for (int t(ProductionStart); t < ProductionStop; t++)
	{
		LoadFactor_Profile[t] = LoadFactor;
		Energy_Profile[t]     = ElectricPower * LoadFactor_Profile[t] * 1e-6 * 365.25 * 24.; // MWh
	}

	// --------------------------------
	// Investment and Interest Cost
	// --------------------------------
	// OCC = Overnight Construction Cost
	// IDC = Interest During Construction Cost
	// TIC = Total Investment Cost = OCC + IDC

	vector <double> OCC_Profile (NTimeSteps, 0);
	vector <double> TIC_Profile (NTimeSteps, 0);
	vector <double> IDC_Profile (NTimeSteps, 0);

	OCC_Profile = CapexProfile_Gaus(OCC, NTimeSteps, ConstructionPeriod);
	/*
	// Méthode plus officielle => Même résultat
	TIC_Profile = Levelization(OCC_Profile, InterestRate_IDC, ConstructionPeriod);
	for (int t(0); t < NTimeSteps; t++) IDC_Profile[t] = TIC_Profile[t] - OCC_Profile[t];
	*/
	//Methode plus naturelle... Même résultat
	TIC_Profile[0] = OCC_Profile[0];
	for (int t(1); t <= ConstructionPeriod; t++)//
		//for (int t(1); t < ConstructionPeriod; t++) => CHECK si = ou pas !!! Résultat différent ici !!!
	{
		for (int i(0); i < t; i++)
		{
			IDC_Profile[t] += InterestRate_IDC * TIC_Profile[i];
		}
		TIC_Profile[t] = OCC_Profile[t] + IDC_Profile[t];
	}

	double IDC = 0.;
	for (auto& n : IDC_Profile) IDC += n;
	double TIC = 0.;
	for (auto& n : TIC_Profile) TIC += n;

// ###################################################################################
// OUTPUT Calculation
// ###################################################################################

	// --------------------------------
	// Fix Costs Calulation
	// --------------------------------

	/*
		Pour les frais futurs qui nécéssite une provision.
		1 - Je calcule le cout courant @ t = futur
		2 - J'actualise @ t = 0
		3 - Je suppose que j'emprunte @ t= 0 et que je place
		4 - Je paye une charge annuelle constante pendant la production pour rembourser l'emprunt
	*/

	// - Fix Costs - Constant Investment Annuity [Eco energie EDP]
	double FixCost_Investment_Annuity = PMT(DiscountRate_Operation, ProductionPeriod, TIC);
	// - Fix Costs - Provision for dismantling
	double DismantlingCost_Levelized = Lev(DismantlingCost, DiscountRate_LongTerm, ProductionStart, ProductionStop);
	double FixCost_Dismantling = PMT(DiscountRate_Operation, ProductionPeriod, DismantlingCost_Levelized);
	// - Fix Costs - Provision for important maintenance
	double MaintenanceCost_Levelized = Lev(MaintenanceCost, DiscountRate_LongTerm, ProductionStart, ProductionStart + ProductionPeriodBeforeMaintenance);
	double FixCost_Maintenance = PMT(DiscountRate_Operation, ProductionPeriod, MaintenanceCost_Levelized);

	// --------------------------------
	// Costs Share and LCOE Calulation
	// --------------------------------

	double CostElectricity_L, Energy_L = 0;

	vector <double> CostElectricity_Profile(NTimeSteps, 0);
	vector <double> CostElectricity_L_Profile(NTimeSteps, 0);
	vector <double> Energy_L_Profile(NTimeSteps, 0);

	vector <double> FixCost_Investment_L_Profile(NTimeSteps, 0);
	vector <double> FixCost_Dismantling_L_Profile(NTimeSteps, 0);
	vector <double> FixCost_Maintenance_L_Profile(NTimeSteps, 0);
	vector <double> FixCost_OM_L_Profile(NTimeSteps, 0);
	vector <double> VarCost_Fuel_L_Profile(NTimeSteps, 0);
	vector <double> VarCost_SpentFuel_L_Profile(NTimeSteps, 0);

	vector <double> FixCost_Investment_Profile(NTimeSteps, 0);
	vector <double> FixCost_Dismantling_Profile(NTimeSteps, 0);
	vector <double> FixCost_Maintenance_Profile(NTimeSteps, 0);
	vector <double> FixCost_OM_Profile(NTimeSteps, 0);
	vector <double> VarCost_Fuel_Profile(NTimeSteps, 0);
	vector <double> VarCost_SpentFuel_Profile(NTimeSteps, 0);

	double FixCost_Investment_L, FixCost_Dismantling_L, FixCost_Maintenance_L, FixCost_OM_L = 0;
	double VarCost_Fuel_L, VarCost_SpentFuel_L = 0;

	for (int t(ProductionStart); t < ProductionStop; t++)
	{
		FixCost_Investment_Profile[t]  = FixCost_Investment_Annuity;
		FixCost_Dismantling_Profile[t] = FixCost_Dismantling;
		FixCost_Maintenance_Profile[t] = FixCost_Maintenance;
		FixCost_OM_Profile[t]          = FixCost_OM;

		VarCost_Fuel_Profile[t]        = FuelCost * Energy_Profile[t];
		VarCost_SpentFuel_Profile[t]   = SpentFuelProvisionCost * Energy_Profile[t];

		FixCost_Investment_L_Profile[t]  = Lev(FixCost_Investment_Profile[t], DiscountRate_Operation, ProductionStart, t);
		FixCost_Dismantling_L_Profile[t] = Lev(FixCost_Dismantling_Profile[t], DiscountRate_Operation, ProductionStart, t);
		FixCost_Maintenance_L_Profile[t] = Lev(FixCost_Maintenance_Profile[t], DiscountRate_Operation, ProductionStart, t);
		FixCost_OM_L_Profile[t]          = Lev(FixCost_OM_Profile[t], DiscountRate_Operation, ProductionStart, t);

		VarCost_Fuel_L_Profile[t]        = Lev(VarCost_Fuel_Profile[t], DiscountRate_Operation, ProductionStart, t);
		VarCost_SpentFuel_L_Profile[t]   = Lev(VarCost_SpentFuel_Profile[t], DiscountRate_Operation, ProductionStart, t);


		CostElectricity_Profile[t] = FixCost_Investment_Profile[t] +
		                             FixCost_Dismantling_Profile[t] +
		                             FixCost_Maintenance_Profile[t] +
		                             FixCost_OM_Profile[t] +
		                             VarCost_Fuel_Profile[t] +
		                             VarCost_SpentFuel_Profile[t];

		CostElectricity_L_Profile[t] = FixCost_Investment_L_Profile[t] +
		                               FixCost_Dismantling_L_Profile[t] +
		                               FixCost_Maintenance_L_Profile[t] +
		                               FixCost_OM_L_Profile[t] +
		                               VarCost_Fuel_L_Profile[t] +
		                               VarCost_SpentFuel_L_Profile[t];

		Energy_L_Profile[t] = Lev(Energy_Profile[t], DiscountRate_Operation, ProductionStart, t);
	}

	for (auto& n : CostElectricity_L_Profile) CostElectricity_L += n;
	for (auto& n : Energy_L_Profile) Energy_L += n;

	for (auto& n : FixCost_Investment_L_Profile) FixCost_Investment_L += n;
	for (auto& n : FixCost_Dismantling_L_Profile) FixCost_Dismantling_L += n;
	for (auto& n : FixCost_Maintenance_L_Profile) FixCost_Maintenance_L += n;
	for (auto& n : FixCost_OM_L_Profile) FixCost_OM_L += n;
	for (auto& n : VarCost_Fuel_L_Profile) VarCost_Fuel_L += n;
	for (auto& n : VarCost_SpentFuel_L_Profile) VarCost_SpentFuel_L  += n;

	double LCOE = CostElectricity_L / Energy_L;

	double Share_Capital     = FixCost_Investment_L / CostElectricity_L * 100;
	double Share_Dismantling = FixCost_Dismantling_L / CostElectricity_L * 100;
	double Share_Maintenance = FixCost_Maintenance_L / CostElectricity_L * 100;
	double Share_FixOM       = FixCost_OM_L / CostElectricity_L * 100;
	double Share_Fuel        = VarCost_Fuel_L / CostElectricity_L * 100;
	double Share_SpentFuel   = VarCost_SpentFuel_L / CostElectricity_L * 100;

// ###################################################################################
// OUTPUT Writing
// ###################################################################################

	cout << endl << "######################################" << endl;
	//cout << "Total Electricity Production : " <<  << " MWh" << endl;
	//cout << "Levelized Total Electricity Production : " <<  << " MWh" << endl;
	//cout << "Total Cost Of Production : " <<  << " €" << endl;
	//cout << "Levelized Total Cost Of Production : " <<  << " €" << endl;
	cout << " => LCOE : " << LCOE << " Euro / MWh" << endl;
	cout << "######################################" << endl << endl;

	cout << "######################################" << endl;
	cout << "Cost" << endl;
	cout << "######################################" << endl;
	cout << "------------------------------" << endl;
	cout << "Investment data" << endl;
	cout << "------------------------------" << endl;
	cout << "Overnight Construction Cost......... : " << OCC / 1e9 << " Md €" << endl;
	cout << "Interest During Construction (at " << 100.*InterestRate_IDC << "%) : " << IDC / 1e9 << " Md €" << endl;
	cout << "Total Investment Cost............... : " << TIC / 1e9 << " Md €" << endl;
	cout << "Fix Cost - Annuity from TStart (at " << 100.*DiscountRate_Operation << "%). : " << FixCost_Investment_Annuity / 1e9 << " Md € / year" << endl;
	cout << "------------------------------" << endl;
	cout << "Maintenance at t = " << ProductionPeriodBeforeMaintenance << endl;
	cout << "------------------------------" << endl;
	cout << "Maintenance  Cost........................... : " << MaintenanceCost / 1e9 << " Md €" << endl;
	cout << "Levelized Maintenance  Cost................. : " << MaintenanceCost_Levelized / 1e9 << " Md €" << endl;
	cout << "Fix Cost - Provision at TStart (at " << 100.*DiscountRate_Operation << "%) : " << FixCost_Maintenance / 1e9 << " Md € / year" << endl;
	cout << "------------------------------" << endl;
	cout << "Dismantling" << endl;
	cout << "------------------------------" << endl;
	cout << "Dismantling Cost............................. : " << DismantlingCost / 1e9 << " Md €" << endl;
	cout << "Levelized Dismantling Cost (at " << 100.*DiscountRate_LongTerm << "%)........... : " << DismantlingCost_Levelized / 1e9 << " Md €" << endl;
	cout << "Fix Cost - Provision at TStart (at " << 100.*DiscountRate_Operation << "%)  : " << FixCost_Dismantling / 1e9 << " Md € / year" << endl;
	cout << "------------------------------" << endl;
	cout << "Operation Fix" << endl;
	cout << "------------------------------" << endl;
	cout << "Fix Operation Cost : " << FixCost_OM / 1e9 << " Md € / year" << endl;
	cout << "------------------------------" << endl;
	cout << "Fuel" << endl;
	cout << "------------------------------" << endl;
	cout << "Var Fuel Cost..................... : " << FuelCost << " € / MWh" << endl;
	cout << "Var Fuel Cost charge @TStart...... : " << FuelCost * Energy_Profile[ProductionStart] << " € / year" << endl;
	cout << "Var Spent Fuel Cost............... : " << SpentFuelProvisionCost << " € / MWh" << endl;
	cout << "Var Spent Fuel Cost charge @TStart : " << SpentFuelProvisionCost * Energy_Profile[ProductionStart] << " € / year" << endl;
	cout << "######################################" << endl;

	cout << endl << "######################################" << endl;
	cout << "Share" << endl;
	cout << "######################################" << endl;
	cout << "Capital     : " << Share_Capital << " %" << endl;
	cout << "Dismantling : " << Share_Dismantling << " %" << endl;
	cout << "Maintenance : " << Share_Maintenance << " %" << endl;
	cout << "Fix OM      : " << Share_FixOM << " %" << endl;
	cout << "Fuel        : " << Share_Fuel << " %" << endl;
	cout << "Spent Fuel  : " << Share_SpentFuel << " %" << endl;
	cout << endl;
	cout << endl << "######################################" << endl;

	// #################################################################################
	// Build histo
	// #################################################################################

	TObjArray Hlist(0); // create an array of Histograms
	THStack *hs_Cost = new THStack("HSCost", "Nuclear Cost");
	TH1F* h;

	TLegend *TL_Cost = new TLegend(0.3, 0.55, 0.9, 0.9);
	TLegend *TL_Prod = new TLegend(0.5, 0.75, 0.9, 0.9);

	int NBinsX = ConstructionPeriod + ProductionPeriod;
	double XBinMin = -ConstructionPeriod - 0.5;
	double XBinMax = ProductionPeriod - 0.5;

	// Construction
	h = new TH1F("TH_OCC", "Construction Cost Profile", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kOrange);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, OCC_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "OCC Profile", "f");
	/*
		h = new TH1F("TH_IDC", "IDC Profile", NBinsX, XBinMin, XBinMax);
		h->SetFillColor(kOrange + 7);
		for (auto t(0); t < NTimeSteps; t++) h->AddBinContent(t + 1, IDC_Profile[t]);
		hs_Cost->Add(h);
		TL_Cost->AddEntry(h, "IDC Profile", "f");
	*/
	// - Fix Charges - Capital loan
	h = new TH1F("TH_Cap", "Investment charges Levelized", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kRed + 2);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, FixCost_Investment_L_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "Investment charges - Lev.", "f");

	// - Fix Charges - Dismantling
	h = new TH1F("TH_Dis", "Dismantling charges Levelized", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kRed);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, FixCost_Dismantling_L_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "Dismantling charges - Lev.", "f");

	// - Fix Charges - Maintenance
	h = new TH1F("TH_Maint", "Maintenance charges Levelized", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kRed - 6);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, FixCost_Maintenance_L_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "Maintenance charges - Lev.", "f");

	// - Fix Charges - OM
	h = new TH1F("TH_FixOM", "Fix OM charges Levelized", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kRed - 6);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, FixCost_OM_L_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "Fix OM charges - Lev.", "f");

	// - Var Costs - Fuel
	h = new TH1F("TH_Fuel", "Fuel Levelized", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kGreen + 1);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, VarCost_Fuel_L_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "Fuel Lev.", "f");

	// - Var Costs - Spent Fuel managment provisions
	h = new TH1F("TH_SpentFuel", "Fuel Managment Prov.", NBinsX, XBinMin, XBinMax);
	h->SetFillColor(kGreen + 3);
	for (auto t(0); t < NBinsX; t++) h->AddBinContent(t + 1, VarCost_SpentFuel_L_Profile[t]);
	hs_Cost->Add(h);
	TL_Cost->AddEntry(h, "Fuel Managment Prov. - Lev.", "f");

	// Energy
	TH1F *h_E = new TH1F("TH_E", "Energy", NBinsX, XBinMin, XBinMax);
	for (auto t(0); t < NBinsX; t++) h_E->AddBinContent(t + 1, Energy_Profile[t]);
	h_E->SetLineColor(kBlue + 1);
	h_E->SetLineWidth(4);
	TL_Prod->AddEntry(h_E, "energy Production", "l");

	TH1F *h_E_L = new TH1F("TH_E_L", "Energy", NBinsX, XBinMin, XBinMax);
	for (auto t(0); t < NBinsX; t++) h_E_L->AddBinContent(t + 1, Energy_L_Profile[t]);
	h_E_L->SetLineColor(kBlue - 9);
	h_E_L->SetLineWidth(4);
	TL_Prod->AddEntry(h_E_L, "energy Production Lev", "l");

	// #################################################################################
	// Plot Histo
	// #################################################################################

	TCanvas *C1 = new TCanvas("C1", "Nuclear Cost", 10, 10, 1200, 900);
	C1->Divide(2, 2);
	C1->cd(1);

	TH1F *h_Template = new TH1F("h_template", "Cost", 25, -ConstructionPeriod * 1.25, ProductionPeriod * 1.25);
	h_Template->GetYaxis()->SetRangeUser(0, TIC * 1.5 / ConstructionPeriod);
	h_Template->GetXaxis()->SetTitle("Time (y)");
	h_Template->GetYaxis()->SetTitle("Price (Euro)");
	h_Template->SetStats(0);
	h_Template->Draw();
	TL_Cost->Draw();

	hs_Cost->Draw("same");
	C1->Modified(); C1->Update();

	C1->cd(3);

	TH1F *h_Template2 = new TH1F("h_template2", "Energy", 25, -ConstructionPeriod * 1.25, ProductionPeriod * 1.25);
	h_Template2->GetYaxis()->SetRangeUser(0, Energy_Profile[ConstructionPeriod] * 1.5);
	h_Template2->GetXaxis()->SetTitle("Time (y)");
	h_Template2->GetYaxis()->SetTitle("Energy (MWh_{electric})");
	h_Template2->SetStats(0);
	h_Template2->Draw();

	TL_Prod->Draw();
	h_E->Draw("same");
	h_E_L->Draw("same");
	C1->Modified(); C1->Update();

	// #################################################################################
	// Plot LCOE data and Pie
	// #################################################################################

	C1->cd(4);

	TPaveText *pt = new TPaveText(0.05, 0.10, 0.95, 0.90);

	pt->AddText("__________ Simulation parameters __________");
	pt->AddText("----- Reactor -----");
	pt->AddText(Form("Construction :  %d", ConstructionPeriod));
	pt->AddText(Form("Production   :  %d", ProductionPeriod));
	pt->AddText(Form("Elec. Power  :  %f", ElectricPower));
	pt->AddText("----- Financing -----");
	pt->AddText(Form("Discount Rate Op :  %f", DiscountRate_Operation));
	pt->AddText(Form("Interest Rate :  %f", InterestRate_IDC));
	pt->AddText("----- Costs -----");
	pt->AddText(Form("OCC :  %f", OCC));
	pt->AddText(Form("TIC :  %f", TIC));
	pt->AddText(Form("LCOE :  %f", LCOE));
	pt->Draw();
	C1->Modified(); C1->Update();


	C1->cd(2);

	Double_t *vals = new Double_t[6];
	vals[0] = Share_Capital    ;
	vals[1] = Share_Dismantling;
	vals[2] = Share_Maintenance;
	vals[3] = Share_FixOM       ;
	vals[4] = Share_Fuel       ;
	vals[5] = Share_SpentFuel  ;

	Int_t colors[] = {kRed + 2, kBlue - 9, kOrange, kGreen + 3, kOrange + 7, kBlue};

	TPie *pie = new TPie("pie", "Cost share", 6, vals, colors);
	pie->SetEntryLabel(0, "Capital");     pie->SetEntryRadiusOffset(0, .05);
	pie->SetEntryLabel(1, "Dismantling"); pie->SetEntryRadiusOffset(0, .05);
	pie->SetEntryLabel(2, "Maintenance"); pie->SetEntryRadiusOffset(0, .05);
	pie->SetEntryLabel(3, "Fix OM");      pie->SetEntryRadiusOffset(0, .05);
	pie->SetEntryLabel(4, "Fuel");        pie->SetEntryRadiusOffset(0, .05);
	pie->SetEntryLabel(5, "SpentFuel");   pie->SetEntryRadiusOffset(0, .05);
	pie->SetLabelsOffset(0.05) ;
	//pie->SetEntryLineColor(2, 2); pie->SetEntryLineWidth(2, 5); pie->SetEntryLineStyle(2, 2);
	//pie->SetEntryFillStyle(1, 3030);
	pie->SetCircle(.5, .5, 0.25);
	pie->Draw("nol <");

	C1->Modified(); C1->Update();


	TApp.Run();


	cout << argv[1] << "," << argv[2] << "," << argv[3] << "," << LCOE << endl;

	return 0;
}
/*

#######################
Compilation line :
#######################

g++ -o CoutNuke CoutNuke.cpp -std=c++14 `root-config --cflags --glibs`

#######################
Execution line :
#######################

./CoutNuke

*/







