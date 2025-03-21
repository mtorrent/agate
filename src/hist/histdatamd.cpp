/**
 * @file src/histdatanc.cpp
 *
 * @brief Read a _HIST file in netcdf
 *
 * @author Jordan Bieder <jordan.bieder@cea.fr>
 *
 * @copyright Copyright 2014 Jordan Bieder
 *
 * This file is part of Agate.
 *
 * Agate is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Agate is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Agate.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "hist/histdatamd.hpp"
#include "base/exception.hpp"
#include "base/utils.hpp"
#include "base/phys.hpp"
#include "base/mendeleev.hpp"
#include "base/geometry.hpp"
#include "io/configparser.hpp"
#include "hist/histdatadtset.hpp"
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <cmath>
#ifdef HAVE_OMP
#include "omp.h"
#endif
#ifdef HAVE_FFTW3
#include <fftw3.h>
#endif

using std::abs;
using namespace Agate;

//
HistDataMD::HistDataMD() : HistData(),
  _mdtemp{0},
  _ekin(),
  _velocities(),
  _temperature(),
  _pressure(),
  _entropy()
{
}

HistDataMD::HistDataMD(const HistData& hist) : HistData(hist),
  _mdtemp{0},
  _ekin(),
  _velocities(),
  _temperature(),
  _pressure(),
  _entropy()
{
  _ekin.resize(_ntimeAvail,0.);
  _velocities.resize(_xyz*_natom*_ntimeAvail,0.);
  _temperature.resize(_ntimeAvail,0.);
  _pressure.resize(_ntimeAvail,0.);
  _entropy.resize(_ntimeAvail,0.);
}

//
HistDataMD::HistDataMD(HistData&& hist) : HistData(hist),
  _mdtemp{0},
  _ekin(),
  _velocities(),
  _temperature(),
  _pressure(),
  _entropy()
{
  _ekin.resize(_ntimeAvail,0.);
  _velocities.resize(_xyz*_natom*_ntimeAvail,0.);
  _temperature.resize(_ntimeAvail,0.);
  _pressure.resize(_ntimeAvail,0.);
  _entropy.resize(_ntimeAvail,0.);
}

HistDataMD::HistDataMD(const HistDataMD& hist) : HistData(hist),
  _mdtemp{hist._mdtemp[0],hist._mdtemp[1]},
  _ekin(hist._ekin),
  _velocities(hist._velocities),
  _temperature(hist._temperature),
  _pressure(hist._pressure),
  _entropy(hist._entropy)
{
}

//
HistDataMD::HistDataMD(HistDataMD&& hist) : HistData(hist),
  _mdtemp{hist._mdtemp[0],hist._mdtemp[1]},
  _ekin(hist._ekin),
  _velocities(hist._velocities),
  _temperature(hist._temperature),
  _pressure(hist._pressure),
  _entropy(hist._entropy)
{
}

//
HistDataMD::~HistDataMD() {
#ifdef HAVE_CPPTHREAD
  if ( _thread.joinable() ) {
    _endThread = true;
    _thread.join();
  }
#endif
}

HistDataMD& HistDataMD::operator = (HistData&& hist){
  this->HistData::operator=(hist);
  _ekin.resize(_ntimeAvail,0.);
  _velocities.resize(_xyz*_natom*_ntimeAvail,0.);
  _temperature.resize(_ntimeAvail,0.);
  _pressure.resize(_ntimeAvail,0.);
  _entropy.resize(_ntimeAvail,0.);

  return *this;
}

HistDataMD& HistDataMD::operator = (const HistData& hist){
  this->HistData::operator=(hist);
  _ekin.resize(_ntimeAvail,0.);
  _velocities.resize(_xyz*_natom*_ntimeAvail,0.);
  _temperature.resize(_ntimeAvail,0.);
  _pressure.resize(_ntimeAvail,0.);
  _entropy.resize(_ntimeAvail,0.);

  return *this;
}

HistDataMD& HistDataMD::operator = (HistDataMD&& hist){
  this->HistData::operator=(hist);
  _ekin = hist._ekin;
  _velocities = hist._velocities;
  _temperature = hist._temperature;
  _pressure = hist._pressure;
  _entropy = hist._entropy;

  return *this;
}

HistDataMD& HistDataMD::operator = (const HistDataMD& hist){
  this->HistData::operator=(hist);
  _ekin = hist._ekin;
  _velocities = hist._velocities;
  _temperature = hist._temperature;
  _pressure = hist._pressure;
  _entropy = hist._entropy;

  return *this;
}

//
const double* HistDataMD::getVel(unsigned time) const {
  if ( time > _ntime )
    throw EXCEPTION(std::string("Out of range for velocities ")+utils::to_string(time)+
        std::string("/")+utils::to_string(_ntime),ERRDIV);
  return &_velocities[time*3*_natom];
}

//
double HistDataMD::getEkin(unsigned time) const {
  if ( time > _ntime )
    throw EXCEPTION(std::string("Out of range for ekin ")+utils::to_string(time)+
        std::string("/")+utils::to_string(_ntime),ERRDIV);
  return _ekin[time];
}

//
double HistDataMD::getTemperature(unsigned time) const {
  if ( time > _ntime )
    throw EXCEPTION(std::string("Out of range for temperature")+utils::to_string(time)+
        std::string("/")+utils::to_string(_ntime),ERRDIV);
  return _temperature[time];
}

//
double HistDataMD::getPressure(unsigned time) const {
  if ( time > _ntime )
    throw EXCEPTION(std::string("Out of range for pressure")+utils::to_string(time)+
        std::string("/")+utils::to_string(_ntime),ERRDIV);
  return _pressure[time];
}

//
HistData& HistDataMD::operator+=(HistData& hist) {
  HistData::operator+=(hist); // _ntime is changed

  if ( HistDataMD *toadd = dynamic_cast<HistDataMD*>(&hist) ) { // complete field (_ntime != _temperature.size())
    *this+=*toadd;
  }
  else{
    _ekin.resize(_ntime,0.);
    _temperature.resize(_ntime,0.);
    _pressure.resize(_ntime,0.);
    _velocities.resize(_ntime*_natom*_xyz,0.);
    _entropy.resize(_ntime,0.);
  }

  return *this;
}

//
HistDataMD& HistDataMD::operator+=(HistDataMD& hist) {
  unsigned prevNtime = _temperature.size();
  if ( _ntime > 1 && hist._ntime >1 ) {
    double dt1 = _time[1]-_time[0];
    double dt2 = hist._time[1]-hist._time[0];
    if ( abs(dt1-dt2) > 1e-6 ) {
      Exception e(EXCEPTION("dtion are different !!! BE VERY CAREFULL FOR ANALYSIS !!!",ERRWAR));
      std::cout << e.fullWhat() << std::endl;
    }
  }

  // Direct call to this function so _ntime == _temperature.size() and we need to add HistData fields first
  if ( prevNtime == _ntime ) HistData::operator+=(*dynamic_cast<HistData*>(&hist));

  bool dovel = _velocities.empty() ^ hist._velocities.empty();
  if ( dovel && _velocities.empty() ) _velocities.resize(_xyz*_natom*prevNtime,0);
  if ( dovel && hist._velocities.empty() ) hist._velocities.resize(_xyz*_natom*hist._ntimeAvail,0);
  dovel = !(_velocities.empty() || hist._velocities.empty());

  _ekin.resize(_ntime);
  std::copy(hist._ekin.begin(), hist._ekin.end(),_ekin.begin()+prevNtime);

  auto first =_temperature.begin();
  auto last = _temperature.end();
  double meanT1 = utils::mean(first,last);
  first = hist._temperature.begin();
  last = hist._temperature.end();
  double meanT2 = utils::mean(first,last);
  if ( abs(meanT1-meanT2)/meanT1 > 0.5 ) {
    Exception e(EXCEPTION("Temperatures seem very different (+50%) !!! BE VERY CAREFULL FOR ANALYSIS !!!",ERRWAR));
    std::cout << e.fullWhat() << std::endl;
  }

  _temperature.resize(_ntime);
  std::copy(hist._temperature.begin(), hist._temperature.end(),_temperature.begin()+prevNtime);

  first = _pressure.begin();
  last = _pressure.end();
  double meanP1 = utils::mean(first,last);
  first = hist._pressure.begin();
  last = hist._pressure.end();
  double meanP2 = utils::mean(first,last);
  if ( abs(meanP1-meanP2)/meanP1 > 0.5 ) {
    Exception e(EXCEPTION("Pressures seem very different (+50%) !!! BE VERY CAREFULL FOR ANALYSIS !!!",ERRWAR));
    std::cout << e.fullWhat() << std::endl;
  }

  std::vector<unsigned> order;
  bool reorder = false;
  if ( _tryToMap ) {
    try {
      order = this->reorder(hist);
      for ( unsigned i = 0 ; i < order.size() ; ++i )
        if ( i != order[i] ) {
          reorder = true;
          break;
        }

    }
    catch ( Exception &e ){
      // This should never happen since it has already been done before !
      e.ADD("Unable to map structures",ERRABT);
    }
  }

  _pressure.resize(_ntime);
  std::copy(hist._pressure.begin(), hist._pressure.end(),_pressure.begin()+prevNtime);

  if ( dovel ) {
    _velocities.resize(_ntime*_natom*_xyz);
    if ( !reorder ) {
      std::copy(hist._velocities.begin(), hist._velocities.end(),_velocities.begin()+prevNtime*_natom*_xyz);
    }
    else {
      unsigned start = prevNtime*_natom*_xyz;
      for ( unsigned itime = 0 ; itime < hist._ntime ; ++itime ) {
        for ( unsigned iatom = 0 ; iatom < _natom ; ++iatom ) {
          for ( unsigned coord = 0 ; coord < 3 ; ++coord ) {
            _velocities[start+itime*3*_natom+iatom*3+coord] = hist._velocities[itime*3*_natom+order[iatom]*3+coord];
          }
        }
      }
    }
  }

  _entropy.resize(_ntime);
  std::copy(hist._ekin.begin(), hist._ekin.end(),_ekin.begin()+prevNtime);

  return *this;
}

//
void HistDataMD::printThermo(unsigned tbegin, unsigned tend, std::ostream &out) {
  using std::ios;
  using std::endl;
  using std::setw;
  using utils::mean;
  using utils::deviation;

  try {
    HistData::checkTimes(tbegin,tend);
  }
  catch (Exception &e) {
    e.ADD("Thermodynamics calculations aborted",ERRDIV);
    throw e;
  }

  std::vector<double>::iterator first;
  std::vector<double>::iterator last;

  first = _etotal.begin();
  std::advance(first,tbegin);
  last = _etotal.begin();
  std::advance(last,tend);
  const double meanE = mean(first,last);
  const double devE = deviation(first,last,meanE);

  first =_temperature.begin();
  std::advance(first,tbegin);
  last = _temperature.begin();
  std::advance(last,tend);
  const double meanT = mean(first,last);
  const double devT = deviation(first,last,meanT);

  first = _pressure.begin();
  std::advance(first,tbegin);
  last = _pressure.begin();
  std::advance(last,tend);
  const double meanP = mean(first,last);
  const double devP = deviation(first,last,meanP);


  std::vector<double> volume(tend-tbegin);

  for ( unsigned itime = tbegin ; itime < tend ; ++itime ) {
    geometry::mat3d rprimd;
    std::copy(&_rprimd[itime*3*3],&_rprimd[itime*3*3+9],rprimd.begin());
    volume[itime-tbegin] = geometry::det(rprimd);
  }

  const double meanV = mean(volume.begin(),volume.end());
  const double devV = deviation(volume.begin(),volume.end(),meanV);


  double meanS[6] = {0.};
  double devS[6] = {0.};

  for ( unsigned s = 0 ; s < 6 ; ++s ) {
    std::vector<double> stress(tend-tbegin);
    for ( unsigned itime = tbegin ; itime < tend ; ++itime )
      stress[itime-tbegin] = _stress[itime*6+s];
    meanS[s] = mean(stress.begin(),stress.end());
    devS[s] = deviation(stress.begin(),stress.end(),meanS[s]);
    meanS[s] *= phys::Ha/(phys::b2A*phys::b2A*phys::b2A)*1e21;
    devS[s] *= phys::Ha/(phys::b2A*phys::b2A*phys::b2A)*1e21;
  }


  out << endl;
  out.setf(ios::scientific,ios::floatfield);
  out.setf(ios::left,ios::adjustfield);
  out.precision(5);
  out << " -- Thermodynamics information --" << endl;
  out << "    ^^^^^^^^^^^^^^^^^^^^^^^^^^   " << endl;
  out.setf(ios::left,ios::adjustfield); out << setw(25) << " Total energy [Ha]:"; out.setf(ios::right,ios::adjustfield); out<< setw(12) << meanE << " +/- " << setw(12) << devE << endl;
  out.setf(ios::left,ios::adjustfield); out << setw(25) << " Volume [Bohr^3]: " ; out.setf(ios::right,ios::adjustfield); out<< setw(12) << meanV << " +/- " << setw(12) << devV << endl;
  out.setf(ios::left,ios::adjustfield); out << setw(25) << " Temperature [K]: " ; out.setf(ios::right,ios::adjustfield); out<< setw(12) << meanT << " +/- " << setw(12) << devT << endl;
  out.setf(ios::left,ios::adjustfield); out << setw(25) << " Pressure [GPa]: "  ; out.setf(ios::right,ios::adjustfield); out<< setw(12) << meanP << " +/- " << setw(12) << devP << endl;
  for ( unsigned s = 0 ; s < 6 ; ++s ) {
    out.setf(ios::left,ios::adjustfield); out << setw(25) << " Stress "+utils::to_string(s+1) + std::string(" [GPa]: ")  ; out.setf(ios::right,ios::adjustfield); out<< setw(12) << meanS[s] << " +/- " << setw(12) << devS[s] << endl;
  }
  out << endl;
}

//
void HistDataMD::plot(unsigned tbegin, unsigned tend, std::istream &stream, Graph *gplot, Graph::Config &config) {
  std::string function;
  auto pos = stream.tellg();

      stream >> function;

      std::string line;
      std::getline(stream,line);
      stream.clear();
      stream.seekg(pos);
      ConfigParser parser;
      parser.setSensitive(true);
      parser.setContent(line);

      unsigned ntime = tend-tbegin;
      std::vector<double> &x = config.x;
      std::list<std::vector<double>> &y = config.y;
      auto &labels = config.labels;
      std::string &filename = config.filename;
      std::string &xlabel = config.xlabel;
      std::string &ylabel = config.ylabel;
      std::string &title = config.title;
      config.doSumUp = true;

      try {
        std::string tunit = parser.getToken<std::string>("tunit");
        if ( tunit == "fs" ) {
          xlabel = "Time [fs]";
          x.resize(ntime);
          for ( unsigned i = tbegin ; i < tend ; ++i ) x[i-tbegin]=_time[i]*phys::atu2fs;
        }
        else if ( tunit == "step" ) {
          xlabel = "Time [step]";
          x.resize(ntime);
          for ( unsigned i = tbegin ; i < tend ; ++i ) x[i-tbegin]=i;
        }
        else {
          throw EXCEPTION("Unknow time unit, allowed values fs and step",ERRDIV);
        }
      }
      catch (Exception &e) {
        xlabel = "Time [step]";
        x.resize(ntime);
        for ( unsigned i = tbegin ; i < tend ; ++i ) x[i-tbegin]=i;
      }

      // TEMPERATURE
      if ( function == "T" ) {
        filename = "temperature";
        ylabel = "Temperature [K]";
        title = "Temperature";
        std::clog << std::endl << " -- Temperature --" << std::endl;
        y.push_back(std::vector<double>(_temperature.begin()+tbegin,_temperature.end()-(_ntime-tend)));
      }

      // Pressure
      else if ( function == "P" ) {
        filename = "pressure";
        ylabel = "Pressure [GPa]";
        title = "Pressure";
        std::clog << std::endl << " -- Pressure --" << std::endl;
        y.push_back(std::vector<double>(_pressure.begin()+tbegin,_pressure.end()-(_ntime-tend)));
      }

      // Ekin
      else if ( function == "ekin" ) {
        filename = "ekin";
        ylabel = "Ekin [Ha]";
        title = "Kinetic energy";
        std::clog << std::endl << " -- Kinetic energy --" << std::endl;
        y.push_back(std::vector<double>(_ekin.begin()+tbegin,_ekin.end()-(_ntime-tend)));
      }

      // Entropy
      else if ( function == "entropy" ) {
        filename = "entropy";
        ylabel = "Entropy";
        title = "Electronic entropy";
        std::clog << std::endl << " -- Electronic entropy --" << std::endl;
        y.push_back(std::vector<double>(_entropy.begin()+tbegin,_entropy.end()-(_ntime-tend)));
      }

      // VACF
      else if ( function == "vacf" ){
        filename = "VACF";
        xlabel = "Time [ps]";
        ylabel = "VACF [nm^2/ps^2/atom]";
        title = "VACF";
        std::clog << std::endl << " -- VACF --" << std::endl;

        y = this->getVACF(tbegin,tend);

        const double dtion = phys::atu2fs*1e-3*( _time.size() > 1 ? _time[1]-_time[0] : 100);
        x.resize(y.front().size());
        for ( unsigned itime = 0 ; itime < y.front().size() ; ++itime )
          x[itime] = itime*dtion;

        for ( unsigned typ = 0 ; typ < y.size() ; ++typ ) {
          if ( typ == 0 )
            labels.push_back("All");
          else
            labels.push_back(utils::trim(std::string(Mendeleev::name[_znucl[typ-1]])));
        }
      }

      // PDOS
      else if ( function == "pdos" ){
        filename = "PDOS";
        ylabel = "PDOS [arbitrary units/atom]";
        xlabel = "Frequency [meV]";
        title = "PDOS";
        std::clog << std::endl << " -- PDOS --" << std::endl;

        const double dtion = phys::atu2fs*1e-3*( _time.size() > 1 ? _time[1]-_time[0] : 100); //ps

        auto first =_temperature.begin();
        std::advance(first,tbegin);
        auto last = _temperature.begin();
        std::advance(last,tend);
        const double T = utils::mean(first,last);

        double smearing = 0;
        try {
          smearing = parser.getToken<double>("tsmear");
        }
        catch ( Exception &e ) {
          smearing = 0.05*T;
        }
        if ( smearing < 0 )
          throw EXCEPTION("tsmear needs to be positive",ERRDIV);
        std::clog << "Smearing [K]: " << smearing << std::endl;

        smearing *= (phys::kB/phys::eV*1e3)/(phys::THz2Ha * phys::Ha2eV *1e3)*(dtion*2); // kBT(ev) / Scaling x axis

        y = this->getPDOS(tbegin,tend,smearing);

        x.resize(y.front().size());
        for ( unsigned itime = 0 ; itime < y.front().size() ; ++itime )
          x[itime] = phys::THz2Ha * phys::Ha2eV *1e3 * itime/(dtion*y.front().size()*2); // *2 because of niquist frequency

        for ( unsigned typ = 0 ; typ < y.size() ; ++typ ) {
          if ( typ == 0 )
            labels.push_back("All");
          else
            labels.push_back(utils::trim(std::string(Mendeleev::name[_znucl[typ-1]])));
        }

        first =_etotal.begin();
        std::advance(first,tbegin);
        last = _etotal.begin();
        std::advance(last,tend);
        const double Etotal = utils::mean(first,last)*phys::Ha2eV/_natom;

        auto pdos_tmp = y.front();
        auto thermo = this->computeThermoFunctionHA(pdos_tmp,T);
        std::cout << "Thermodynamic functions in the Harmonic Approximation " << std::endl;
        std::cout << "E_0   = " << Etotal    << " eV/atom" << std::endl;
        std::cout << "F_vib = " << thermo[0] << " eV/atom" << std::endl;
        std::cout << "E_vib = " << thermo[1] << " eV/atom" << std::endl;
        std::cout << "C_v   = " << thermo[2] << " kB/atom" << std::endl;
        std::cout << "S_vib = " << thermo[3] << " kB/atom" << std::endl;
        std::cout << "F_tot = " << thermo[0]+Etotal << " kB/atom" << std::endl;
        config.doSumUp = false;
      }

      // thermo
      else if ( function == "thermo" ){
        filename = "thermoFunctions";
        ylabel = "Thermodynamical Functions";
        xlabel = "Temperature [K]";
        title = "Thermodynamical Functions";
        std::clog << std::endl << " -- Thermodynamical Functions --" << std::endl;
        auto first =_temperature.begin();
        std::advance(first,tbegin);
        auto last = _temperature.begin();
        std::advance(last,tend);
        const double T = utils::mean(first,last);
        first =_etotal.begin();
        std::advance(first,tbegin);
        last = _etotal.begin();
        std::advance(last,tend);
        const double Etotal = utils::mean(first,last)*phys::Ha2eV/_natom;
        std::cout << "E_0   = " << Etotal    << " eV/atom" << std::endl;

        x.resize(1000);
        y.resize(4);
        auto tmp = y.begin();
        auto& F = *(tmp);
        auto& E = *(++tmp);
        auto& C = *(++tmp);
        auto& S = *(++tmp);
        F.resize(1000);
        E.resize(1000);
        C.resize(1000);
        S.resize(1000);

        double smearing = 0;
        try {
          smearing = parser.getToken<double>("tsmear");
        }
        catch ( Exception &e ) {
          smearing = 0.05*T;
        }
        if ( smearing < 0 )
          throw EXCEPTION("tsmear needs to be positive",ERRDIV);

        const double dtion = phys::atu2fs*1e-3*( _time.size() > 1 ? _time[1]-_time[0] : 100); //ps
        smearing *= (phys::kB/phys::eV*1e3)/(phys::THz2Ha * phys::Ha2eV *1e3)*(dtion*2); // kBT(ev) / Scaling x axis

        auto pdos = this->getPDOS(tbegin,tend,smearing);

        double dT = 2.*T/1000.;
        for ( unsigned itime = 0 ; itime < 1000 ; ++itime ) {
          x[itime] = (itime+1)*dT;
          auto thermo = this->computeThermoFunctionHA(pdos.front(),x[itime]);
          F[itime] = thermo[0];
          E[itime] = thermo[1];
          C[itime] = thermo[2];
          S[itime] = thermo[3];
        }

        labels.push_back("F_vib [eV/atom]");
        labels.push_back("E_vib [eV/atom]");
        labels.push_back("C_v   [kB/atom]");
        labels.push_back("S_vib [kB/atom]");
        config.doSumUp = false;
      }

      else {
        try {
          HistData::plot(tbegin, tend, stream, gplot, config);
          return;
        } catch (Exception &e) {
          e.ADD(std::string("Function ") + function +
                    std::string(" not available yet"),
                ERRABT);
          throw e;
        }
      }

      try {
        filename = parser.getToken<std::string>("output");
      }
      catch (Exception &e) {
        filename = utils::noSuffix(_filename)+std::string("_")+filename;
      }
      Graph::plot(config,gplot);
      stream.clear();
}

std::list<std::vector<double>> HistDataMD::getVACF(unsigned tbegin, unsigned tend) const {
  auto begin = _velocities.begin();
  std::advance(begin,tbegin*3*_natom);
  auto end = begin;
  const unsigned ntime = tend-tbegin;
  std::advance(end,ntime*3*_natom);

  std::vector<double> fullvacf;
  try {
    fullvacf = HistData::acf(begin,end,3*_natom);
  }
  catch ( Exception &e ) {
    e.ADD("VACF calculation failed",ERRDIV);
    throw e;
  }
  const unsigned ntau = fullvacf.size()/(3*_natom);
  
  // Count number of each type
  std::vector<int> ntypat(_znucl.size()+1);
  for ( unsigned iatom = 0 ; iatom < _natom ; ++iatom )
    ++ntypat[_typat[iatom]];
  ntypat[0] = _natom;

  std::vector<std::vector<double>> vacf_tmp(ntau,std::vector<double>(_znucl.size()+1,0.));
#pragma omp parallel for
  for ( unsigned itau = 0 ; itau < ntau ; ++itau ) {
    auto &vacftau = vacf_tmp[itau];
    const double shift = itau*3*_natom;
    for ( unsigned iatom = 0 ; iatom < _natom ; ++iatom ) {
      for ( unsigned c = 0 ; c < 3 ; ++c ) {
        vacftau[0] += fullvacf[shift+iatom*3+c];
        vacftau[_typat[iatom]] += fullvacf[shift+iatom*3+c];
      }
    }
  }

  const double conversion2nm2ps2 = (phys::b2A*1e-1)*(phys::b2A*1e-1)/(phys::atu2fs*1e-3*phys::atu2fs*1e-3);
  std::list<std::vector<double>> vacf(_znucl.size()+1,std::vector<double>(ntau,0));
  for ( unsigned ityp = 0 ; ityp < _znucl.size()+1 ; ++ityp ) {
    const double natom_3 = 3*ntypat[ityp] ;
    auto lvacf = vacf.begin();
    std::advance(lvacf,ityp);
    for ( unsigned itau = 0 ; itau < ntau ; ++itau ) {
      lvacf->at(itau) += vacf_tmp[itau][ityp]/(natom_3)*conversion2nm2ps2;
    }
  }
  return vacf;
}

std::list<std::vector<double>> HistDataMD::getPDOS(unsigned tbegin, unsigned tend, double tsmear) const {
#ifndef HAVE_FFTW3
  throw EXCEPTION("FFTW3 is needed to compute the PDOS",ERRDIV);
  (void) tbegin;
  (void) tend;
#else

  auto pdos = this->getVACF(tbegin,tend);
  const int howmany = pdos.size();
  const int n = pdos.front().size();

  const int idist = n; const int odist = n;
  const int *inembed = NULL; const int *onembed = NULL;
  const int istride = 1; const int ostride = 1;

  double *fft_in;
  double *fft_out;
  fftw_plan plan_forward;

  fft_in = (double*) fftw_malloc( sizeof ( double ) * n * howmany);
  fft_out = (double*) fftw_malloc( sizeof ( double ) * n * howmany);
  fftw_r2r_kind kind = FFTW_REDFT10;

#pragma omp parallel for
  for ( int u = 0 ; u < howmany ; ++u ) {
    auto vacf = pdos.begin();
    std::advance(vacf,u);
    std::copy(vacf->begin(),vacf->end(),&fft_in[u*n]);
  }

#if defined(HAVE_OMP) && defined(HAVE_FFTW3_THREADS)
  fftw_plan_with_nthreads(omp_get_max_threads());
#endif

#pragma omp critical (pdos_fft)
  {
    plan_forward = fftw_plan_many_r2r(1, &n, howmany, 
        fft_in, inembed, istride, idist, 
        fft_out, onembed, ostride, odist, &kind, FFTW_ESTIMATE);
  }

  fftw_execute(plan_forward);

  if ( tsmear > 0 ) {
    const double sigma = tsmear;
    const double renorm = 1./(sigma*std::sqrt(2*phys::pi));
    const double inv_2sigma2 = 1./(2.*sigma*sigma);
    const double inv_n = 1./(double)n;

#pragma omp parallel for
    for ( int u = 0 ; u < howmany ; ++u ) {
      auto ptrpdos = pdos.begin();
      std::advance(ptrpdos,u);

      std::vector<double> &fit = *ptrpdos;
      std::fill(fit.begin(),fit.end(),0.);
      for ( int i = 0 ; i < n ; ++i ) {
        const double mean = i*inv_n;
        const double max = fft_out[u*n+i]*renorm;
        for ( int g = 0; g < n ; ++g ) {
          fit[g]+=max*std::exp(-(g*inv_n-mean)*(g*inv_n-mean)*inv_2sigma2);
        }
      }
    }
  }
  else {
#pragma omp parallel for
    for ( int u = 0 ; u < howmany ; ++u ) {
      auto ptrpdos = pdos.begin();
      std::advance(ptrpdos,u);
      std::copy(&fft_out[u*n],&fft_out[(u+1)*n],ptrpdos->begin());
    }
  }

#pragma omp critical (pdos_fft)
  {
    fftw_destroy_plan(plan_forward);
  }

  fftw_free(fft_in);
  fftw_free(fft_out);

  return pdos;
#endif
}

void HistDataMD::interpolate(unsigned ninter, double amplitude) {
  this->waitTime(_ntime);
  int nsegment = _ntime-1;
  int newNTime = ninter*nsegment;
  unsigned natom3 = 3*_natom;
  bool removeDuplica = false;

  if ( (removeDuplica = (std::abs(amplitude-1) < 1e-10)) ) newNTime -= (_ntime-2); // Remove duplica

  _velocities.resize(natom3*newNTime);
  _ekin.resize(natom3*newNTime);
  _temperature.resize(_xyz*newNTime);
  _pressure.resize(_xyz*_xyz*newNTime);
  _entropy.resize(newNTime);

  double alpha = (amplitude)/(ninter-1);
  unsigned currentTime = newNTime-1;
  for( int lastStep = _ntime-1 ; lastStep > 0 ; --lastStep ) {
    int firstStep = lastStep-1;
    std::vector<double> velocitiesLast(_velocities.begin()+natom3*lastStep,_velocities.begin()+natom3*(lastStep+1));
    double ekinLast = _ekin[lastStep];
    double temperatureLast = _temperature[lastStep];
    double pressureLast = _pressure[lastStep];
    double entropyLast = _entropy[lastStep];
    //std::clog << "Between " << firstStep << " and " << lastStep << std::endl;
    for ( unsigned tinter = 0 ; tinter < ninter ; ++tinter ) {
      double beta = tinter*alpha;
      double gamma = 1-beta;
      //std::clog << "point " << tinter << " pour time " << currentTime << " : "  << gamma << "*last+" << beta << "*first" << std::endl;
      for ( unsigned iatomDir = 0 ; iatomDir < natom3 ; ++iatomDir ) {
        _velocities[natom3*currentTime+iatomDir] =
            gamma*velocitiesLast[iatomDir]
            +beta*_velocities[natom3*firstStep+iatomDir];
      }
      _ekin[currentTime] =
          gamma*ekinLast+beta*_ekin[firstStep];
      _temperature[currentTime] =
          gamma*temperatureLast+beta*_temperature[firstStep];
      _pressure[currentTime] =
          gamma*pressureLast+beta*_pressure[firstStep];
      _entropy[currentTime] =
          gamma*entropyLast+beta*_entropy[firstStep];
      --currentTime;
    }
    if (removeDuplica) ++currentTime; // Override previous step since will be the same;
  }
  HistData::interpolate(ninter,amplitude);
}

void HistDataMD::computeVelocitiesPressureTemperature(unsigned itime, double dtion) {
  if ( itime >= 2 ) { // Estimate velocities at itime-1
    for (unsigned iatom = 0 ; iatom < _natom ; ++iatom) {
      for ( unsigned c = 0 ; c < 3 ; ++c ) 
        _velocities[(itime-1)*3*_natom+iatom*3+c] = 0.5*(_xcart[(itime*_natom+iatom)*3+c]-_xcart[((itime-2)*_natom+iatom)*3+c])/dtion;
    }
    this->computePressureTemperature(itime-1);
  }
  if ( itime == _ntime-1 && itime > 0 ) {
    for (unsigned iatom = 0 ; iatom < _natom ; ++iatom) {
      for ( unsigned c = 0 ; c < 3 ; ++c ) 
        _velocities[itime*3*_natom+iatom*3+c] = (_xcart[(itime*_natom+iatom)*3+c]-_xcart[((itime-1)*_natom+iatom)*3+c])/dtion;
    }
    this->computePressureTemperature(itime);
  }
  if ( itime == 1 ) {
    for (unsigned iatom = 0 ; iatom < _natom ; ++iatom) {
      for ( unsigned c = 0 ; c < 3 ; ++c ) 
        _velocities[0*3*_natom+iatom*3+c] = (_xcart[(itime*_natom+iatom)*3+c]-_xcart[((itime-1)*_natom+iatom)*3+c])/dtion;
    }
    this->computePressureTemperature(0);
  }
}

void HistDataMD::computePressureTemperature(unsigned itime) {
  const double factorT = /*2.0 **/ phys::Ha / ( 3.0*phys::kB * static_cast<double>(_natom) );
  const double factorP = phys::kB/phys::Ha;
  const double volume = geometry::det(&_rprimd[itime*9]);

  // _ekin is in Ha, kB is in J/K => temperature is in K.
  //_temperature[itime] = _ekin[itime] * factorT;
  // In PIMD ekin != 1/2 mv^2 So recompute from velocities
  _temperature[itime] = 0;
  for ( unsigned iatom = 0 ; iatom < _natom ; ++iatom ) {
    const double mass = MendeTable.mass[_znucl[_typat[iatom]-1]]*phys::amu_emass;
    double v2 = 0;
    for ( unsigned c = 0 ; c < 3 ; ++c ) {
      v2 += _velocities[itime*_natom*3+iatom*3+c]*_velocities[itime*_natom*3+iatom*3+c];
    }
    _temperature[itime] += mass*v2;
  }
  _temperature[itime] *= factorT;

  //Compute pressure in GPa
  _pressure[itime] = phys::Ha/(phys::b2A*phys::b2A*phys::b2A)*1e21 * (
      -(_stress[itime*6  ]+_stress[itime*6+1]+_stress[itime*6+2])/3.0
      + _natom/volume*factorP*_temperature[itime] );

}

std::array<double,4> HistDataMD::computeThermoFunctionHA(unsigned tbegin, unsigned tend, const double omegaMax) const {
  using std::min;
  try {
    HistData::checkTimes(tbegin,tend);
  }
  catch (Exception &e) {
    e.ADD("Thermodynamics calculations aborted",ERRDIV);
    throw e;
  }

  std::vector<double> pdos;
  try {
    auto pdosfull = this->getPDOS(tbegin,tend,0);
    pdos = std::move(pdosfull.front());
  }
  catch ( Exception &e ) {
    e.ADD("Unable to compute thermodynamics functions.",ERRDIV);
  }

  auto first =_temperature.begin();
  std::advance(first,tbegin);
  auto last = _temperature.begin();
  std::advance(last,tend);
  const double T = utils::mean(first,last);

  return this->computeThermoFunctionHA(pdos,T,omegaMax);
}

std::array<double,4> HistDataMD::computeThermoFunctionHA(std::vector<double> &pdos, const double T, const double omegaMax) const {
  using std::min;
  const unsigned nfreq = pdos.size();
  const double dtion = phys::atu2fs*1e-3*( _time.size() > 1 ? _time[1]-_time[0] : 100); //ps
  const double domega = 1./(2.*dtion*nfreq); // THz
  std::vector<double> omega;
  for ( unsigned itime = 0 ; itime < pdos.size() ; ++itime )
    omega.push_back(phys::THz2Ha * phys::Ha2eV * (itime+0.5) * domega ); // eV
  // omega is shifted on the grid to be at the middle of a segement [i i+1]

  unsigned nmax = ( omegaMax < 0 ? nfreq : min(unsigned(omegaMax/domega),nfreq) );

  // Renormalize the pdos \int_0^nmax pdos domega = 1
  double norme = 0;
  for ( unsigned iomega = 0 ; iomega < nmax-1 ; ++iomega )
    norme += (pdos[iomega]+pdos[iomega+1])*0.5*domega;
  for ( unsigned iomega = 0 ; iomega < nmax ; ++iomega )
    pdos[iomega] /= norme;

  // Compute F E C S
  double F = 0.;
  double E = 0.;
  double C = 0.;
  double S = 0.;
  double kBT = phys::kB*T/phys::eV; // eV
  const double inv_2kBT = 0.5/kBT /* in eV-1 */ ;
  for ( unsigned iomega = 0 ; iomega < nmax-1 ; ++iomega ) {
    using std::log;
    using std::sinh;
    using std::tanh;
    const double argument = omega[iomega]*inv_2kBT;
    const double gwdw = (pdos[iomega]+pdos[iomega+1])*domega*0.5;
    const double sinharg = sinh(argument);
    const double cotharg = 1./tanh(argument);
    const double log2sinharg = log(2.*sinharg);
    F += log2sinharg*gwdw;
    E += omega[iomega]*cotharg*gwdw;
    C += argument*argument/(sinharg*sinharg)*gwdw;
    S += (argument*cotharg-log2sinharg)*gwdw;
  }
  F *= 3*kBT;
  E *= 3*0.5;
  C *= 3.;
  S *= 3.;
  return std::array<double,4> ({{F,E,C,S}});
}

HistData* HistDataMD::average(unsigned tbegin, unsigned tend) const {
  this->checkTimes(tbegin, tend);
  HistDataDtset *av = new HistDataDtset;

  this->myAverage(tbegin,tend,*av);

  double inv_ntime = 1./static_cast<double>(tend-tbegin);

  av->_mdtemp[0] = _mdtemp[0]; 
  av->_mdtemp[1] = _mdtemp[1]; 
  av->_ekin.resize(1,0);
  av->_velocities.resize(_xyz*_natom,0);
  av->_temperature.resize(1,0);
  av->_pressure.resize(1,0);
  av->_entropy.resize(1,0);
  for ( unsigned time = tbegin ; time < tend ; ++time ) {
    for ( unsigned val = 0 ; val < 3*_natom ; ++val ) {
      av->_velocities[val] += _velocities[time*3*_natom+val]*inv_ntime;
    }
    av->_ekin[0] += _ekin[time]*inv_ntime;
    av->_temperature[0] += _temperature[time]*inv_ntime;
    av->_pressure[0] += _pressure[time]*inv_ntime;
    av->_entropy[0] += _entropy[time]*inv_ntime;
  }

  return av;
}
