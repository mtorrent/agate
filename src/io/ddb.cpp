/**
 * @file src/ddb.cpp
 *
 * @brief 
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


#include "io/ddb.hpp"
#include "io/configparser.hpp"
#include "io/ddbabinit.hpp"
#include "io/ddbphonopy.hpp"
#include "io/ddboutcar.hpp"
#include "base/exception.hpp"
#include "base/utils.hpp"
#include "base/uriparser.hpp"
#include "base/fraction.hpp"
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>

//
Ddb::Ddb() : Dtset(),
  _haveMasses(false),
  _nqpt(0),
  _blocks(),
  _zion()
{
  ;
}

//
Ddb::~Ddb() {
  ;
}


//
std::string Ddb::info() const {
  std::ostringstream rstr;
  Dtset::dump(rstr);
  rstr << " ** DDB Information ** \n-> "
    << utils::to_string(_blocks.size())
    << " qpt found.\n";
  rstr.precision(14);
  rstr.setf(std::ios::scientific, std::ios::floatfield);
  rstr.setf(std::ios::right, std::ios::adjustfield);
  for ( auto& block : _blocks ) {
    rstr << std::endl << "Q-pt: " << geometry::to_string(block.first) << std::endl;
    rstr << "  # elements: " << block.second.size() << std::endl;
  }
  return rstr.str();
}


//
const std::vector<Ddb::d2der>& Ddb::getDdb(const geometry::vec3d qpt) const {
  using namespace geometry;
  auto found = _blocks.end();
  for ( auto it = _blocks.begin() ; it != _blocks.end() ; ++ it) {
    if ( norm(qpt-it->first) < 1e-12 )
      found = it;
  }
  if ( found == _blocks.end() )
    throw EXCEPTION(std::string("Block not found for q-pt ")+to_string(qpt), Ddb::ERFOUND);

  return found->second;
}

std::vector<Ddb::d2der>& Ddb::getD2der(const geometry::vec3d qpt) {
  using namespace geometry;
  auto found = _blocks.end();
  for ( auto it = _blocks.begin() ; it != _blocks.end() ; ++ it) {
    if ( norm(qpt-it->first) < 1e-12 )
      found = it;
  }
  if ( found == _blocks.end() ) {
    std::vector<d2der> block;
    _blocks.insert(std::make_pair( qpt, block));
    return _blocks[qpt];
  }
  else {
    return found->second;
  }
}

//
const std::vector<geometry::vec3d> Ddb::getQpts() const {
  std::vector<geometry::vec3d> qpts;
  for ( auto it = _blocks.begin() ; it != _blocks.end() ; ++it ) {
    qpts.push_back(it->first);
  }
  return qpts;
}

//
Ddb* Ddb::getDdb(const std::string& infile){
  Ddb *ddb = nullptr;
  Exception eloc;
  std::vector<std::pair<std::unique_ptr<Ddb>,std::string> > allFormat;

  allFormat.push_back(std::make_pair(std::unique_ptr<Ddb>(new DdbAbinit),"Abinit DDB")); //0
  allFormat.push_back(std::make_pair(std::unique_ptr<Ddb>(new DdbPhonopy),"Phonopy YAML"));   //1
  allFormat.push_back(std::make_pair(std::unique_ptr<Ddb>(new DdbOutcar),"OUTCAR"));   //2

  std::string file = infile;
  UriParser uri;
  if ( uri.parse(infile) ) {
    file = uri.getFile();
  }

  if ( file.find(".yaml") != std::string::npos ) allFormat[0].swap(allFormat[1]);
  else if ( file.find("OUTCAR") != std::string::npos ) allFormat[0].swap(allFormat[2]);

  for ( auto& p : allFormat ) {
    try {
      p.first->readFromFile(file);
      ddb = p.first.release();
    }
    catch (Exception &e) {
      ddb = nullptr;
      eloc += e;
      if ( e.getReturnValue() == ERRABT ) {
        break;
      }
      eloc.ADD("Format is not "+p.second,ERRDIV);
    }
    if ( ddb != nullptr ) {
      std::clog << "Format is "+p.second << std::endl;
      return ddb;
    }
  }

  eloc.ADD("Failed to build the DDB",ERRDIV);
  throw eloc;
  return nullptr;
}

void Ddb::dump(const geometry::vec3d qpt, std::string filename) {
  auto& d2 = this->getDdb(qpt);
  std::ofstream out;

  if ( filename == "" )
    filename = std::string("dynmat-") + utils::to_string(qpt[0]) + "-" +
               utils::to_string(qpt[1]) + "-" + utils::to_string(qpt[2]) +
               ".out";

  out.open(filename);
  
  if ( !out ) throw EXCEPTION(std::string("Unable to create file ")+filename,ERRDIV);

  for ( unsigned iatom1 = 0 ; iatom1 < _natom ; ++iatom1 ) {
    for ( unsigned idir1 = 0 ; idir1 < 3 ; ++idir1 ) {
      for ( unsigned iatom2 = 0 ; iatom2 < _natom ; ++iatom2 ) {
        for ( unsigned idir2 = 0 ; idir2 < 3 ; ++idir2 ) {
          for ( auto &elt : d2 ) {
            auto &coord = elt.first;
            if ( 
                coord[0] == idir1 &&
                coord[1] == iatom1 &&
                coord[2] == idir2 &&
                coord[3] == iatom2
               )
              out << std::setw(12) << elt.second.real() << " " ;
          }
        }
      }
      out << std::endl;
    }
  }
  out.close();
  throw EXCEPTION(std::string("Dynamical matrix written to ")+filename, ERRCOM);
}

geometry::mat3d Ddb::getZeff(const unsigned iatom) const {
  using namespace geometry;
  const vec3d qpt = {{0,0,0}};
  
  if ( iatom >= _natom ) 
    throw EXCEPTION("Atom "+utils::to_string(iatom)+" is not in DDB", ERRDIV);

  auto data = this->getDdb(qpt);
  mat3d zeff = {0};
  mat3d count = {0};
  const double twopi = 2*phys::pi;
	
	/* Read values from ddb into _zeff*/
	for ( auto& elt : data ) {
    const unsigned idir1 = elt.first[0];
    const unsigned ipert1 = elt.first[1];
    const unsigned idir2 = elt.first[2];
    const unsigned ipert2 = elt.first[3];
    if ( idir1 < 3 && idir2 < 3 && 
        ( ( ipert1 == _natom+1 && ipert2 == iatom ) 
          || ( ipert2 == _natom+1 && ipert1 == iatom ) 
        )
       ) {
      // Store Efield along column and disp alon lines
      if ( ipert1 == _natom+1 ) { 
        zeff[mat3dind( idir1+1, idir2+1)] += elt.second.real();
        count[mat3dind( idir1+1, idir2+1)] += 1e0;
      }
      else {
        zeff[mat3dind( idir2+1, idir1+1)] += elt.second.real();
        count[mat3dind( idir2+1, idir1+1)] += 1e0;
      }
    }
	}  	

  mat3d rprimTranspose = transpose(_rprim);
  for ( unsigned i = 1 ; i < 4 ; ++i )
    for ( unsigned j = 1 ; j < 4 ; ++j ) {
      if ( count[mat3dind(i,j)] == 0 )
        throw EXCEPTION("Derivative not found for element "
            +utils::to_string(i)+","+utils::to_string(j),ERRDIV);
      zeff[mat3dind(j,i)] /= (twopi*count[mat3dind(j,i)]);
    }
  
  zeff = _gprim * (zeff * rprimTranspose);

	for ( unsigned idir = 1 ; idir <= 3 ; ++idir ) 
       zeff[mat3dind( idir, idir)] += _zion[_typat[iatom]-1];		

	return zeff;			
}

geometry::mat3d Ddb::getEpsInf() const {
  using namespace geometry;
  const vec3d qpt = {{0,0,0}};
  
  auto data = this->getDdb(qpt);
  mat3d epsinf;
  mat3d count;
  for ( auto &e : count ) e = 0e0;
  for ( auto &e : epsinf ) e = 0e0;
	
	/* Read values from ddb into epsilon infiny*/
	for ( auto& elt : data ) {
    const unsigned idir1 = elt.first[0];
    const unsigned ipert1 = elt.first[1];
    const unsigned idir2 = elt.first[2];
    const unsigned ipert2 = elt.first[3];
    if ( idir1 < 3 && idir2 < 3 &&
        ( ipert1 == _natom+1 && ipert2 == _natom+1 )
       )
       {
      epsinf[mat3dind( idir1+1, idir2+1)] += elt.second.real();
      count[mat3dind( idir1+1, idir2+1)] += 1e0;
    }
	}  	

  mat3d rprimTranspose = transpose(_rprim);
  double volume = det(_rprim);
  for ( unsigned i = 1 ; i < 4 ; ++i )
    for ( unsigned j = 1 ; j < 4 ; ++j ) {
      if ( count[mat3dind(j,i)] == 0 )
        throw EXCEPTION("Derivative not found for element "
            +utils::to_string(i)+","+utils::to_string(j),ERRDIV);
      epsinf[mat3dind(j,i)] /= (-phys::pi*volume*count[mat3dind(j,i)]);
    }
  
  epsinf = _rprim * (epsinf * rprimTranspose);

	for ( unsigned idir = 1 ; idir <= 3 ; ++idir ) 
       epsinf[mat3dind( idir, idir)] += 1.0;

	return epsinf;			
}

void Ddb::blocks2Reduced() {
  complex *matrix = new complex[3*_natom*3*_natom];
  using namespace geometry;
  for ( auto& block : _blocks ) {
    std::fill_n(matrix,3*_natom*3*_natom,complex(0,0));

    std::vector<Ddb::d2der> saved;
    for ( auto& elt : block.second ) {
      const unsigned idir1 = elt.first[0];
      const unsigned ipert1 = elt.first[1];
      const unsigned idir2 = elt.first[2];
      const unsigned ipert2 = elt.first[3];
      if ( !(idir1 < 3 && idir2 < 3 && ipert1 < _natom && ipert2 < _natom) ) {
        saved.push_back(
            std::make_pair(
              std::array<unsigned,4>{{ idir1, ipert1, idir2, ipert2 }},
              elt.second
              )
            );
      }
      else {
        matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+idir2] = elt.second;
      }
    }
    block.second.clear();
    block.second = saved;

    // Go to reduce coordinates
    for ( unsigned ipert1 = 0 ; ipert1 < _natom ; ++ipert1 ) {
      for ( unsigned idir1 = 0 ; idir1 < 3 ; ++idir1 ) {
        for ( unsigned ipert2 = 0 ; ipert2 < _natom ; ++ipert2 ) {
          vec3d d2cartR;
          vec3d d2cartI;
          d2cartR[0] = matrix[(ipert1*3+idir1)*3*_natom + ipert2*3  ].real();
          d2cartI[0] = matrix[(ipert1*3+idir1)*3*_natom + ipert2*3  ].imag();
          d2cartR[1] = matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+1].real();
          d2cartI[1] = matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+1].imag();
          d2cartR[2] = matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+2].real();
          d2cartI[2] = matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+2].imag();
          vec3d d2redRowR = _rprim * d2cartR;
          vec3d d2redRowI = _rprim * d2cartI;
          matrix[(ipert1*3+idir1)*3*_natom + ipert2*3  ].real(d2redRowR[0]);
          matrix[(ipert1*3+idir1)*3*_natom + ipert2*3  ].imag(d2redRowI[0]);
          matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+1].real(d2redRowR[1]);
          matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+1].imag(d2redRowI[1]);
          matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+2].real(d2redRowR[2]);
          matrix[(ipert1*3+idir1)*3*_natom + ipert2*3+2].imag(d2redRowI[2]);
        }
      }
    }
    //Second loop : change basis from reduced to cartesian (columns)
    for ( unsigned ipert1 = 0 ; ipert1 < _natom ; ++ipert1 ) {
      for ( unsigned ipert2 = 0 ; ipert2 < _natom ; ++ipert2 ) {
        for ( unsigned idir2 = 0 ; idir2 < 3 ; ++idir2 ) {
          vec3d d2redRowR;
          vec3d d2redRowI;
          d2redRowR[0] = matrix[(ipert1*3  )*3*_natom + ipert2*3+idir2].real();
          d2redRowI[0] = matrix[(ipert1*3  )*3*_natom + ipert2*3+idir2].imag();
          d2redRowR[1] = matrix[(ipert1*3+1)*3*_natom + ipert2*3+idir2].real(); 
          d2redRowI[1] = matrix[(ipert1*3+1)*3*_natom + ipert2*3+idir2].imag(); 
          d2redRowR[2] = matrix[(ipert1*3+2)*3*_natom + ipert2*3+idir2].real();
          d2redRowI[2] = matrix[(ipert1*3+2)*3*_natom + ipert2*3+idir2].imag();
          vec3d d2redR = _rprim * d2redRowR;
          vec3d d2redI = _rprim * d2redRowI;
          for ( unsigned idir1 = 0 ; idir1 < 3 ; ++idir1 ) {
            block.second.push_back(
                std::make_pair(
                  std::array<unsigned,4>{{ idir1, ipert1, idir2, ipert2 }} , 
                  complex(d2redR[idir1],d2redI[idir1])
                  )
                );
          }
        }
      }
    }
  }
  delete[] matrix;
}

void Ddb::setZeff(const unsigned iatom, const geometry::mat3d &zeff) {
  using namespace geometry;
  const vec3d qpt = {{0,0,0}};

  if ( iatom >= _natom )
    throw EXCEPTION("Atom "+utils::to_string(iatom)+" is not in DDB", ERRDIV);

  std::vector<Ddb::d2der>& block = this->getD2der(qpt);

  const double twopi = 2*phys::pi;

  _zion[_typat[iatom]-1] = 0;

  mat3d rprimTranspose = transpose(_rprim);
  geometry::mat3d d2red = (rprimTranspose * (zeff  * _gprim))*twopi;

  for ( int i = 0 ; i < 2 ; ++i ) {
    for ( unsigned idir1 = 0 ; idir1 < 3 ; ++idir1 ) {
      for ( unsigned idir2 = 0 ; idir2 < 3 ; ++idir2 ) {
        auto alreadyIn = std::find_if(block.begin(),block.end(),[=](
              const std::pair<std::array<unsigned,4>,std::complex<double>>& entry) { 
            auto& array = entry.first;
            return ( array[0] == idir1 && array[1] ==(_natom+1) && array[2] == idir2 && array[3]==iatom) || 
            ( array[0] == idir2 && array[1] ==iatom && array[2] == idir1 && array[3]==(_natom+1)); 
            });
        if ( alreadyIn != block.end() ) block.erase(alreadyIn);
      }
    }
  }

  for ( unsigned idir1 = 0 ; idir1 < 3 ; ++idir1 )
    for ( unsigned idir2 = 0 ; idir2 < 3 ; ++idir2 ) {
      block.push_back(
          std::make_pair(
            std::array<unsigned,4>{{ idir1, _natom+1, idir2, iatom}},
            complex(d2red[mat3dind(idir1+1,idir2+1)],0)
            )
          );
      block.push_back(
          std::make_pair(
            std::array<unsigned,4>{{ idir2, iatom, idir1, _natom+1}},
            complex(d2red[mat3dind(idir1+1,idir2+1)],0)
            )
          );
    }
  auto check = this->getZeff(iatom);
  for ( unsigned i = 0 ; i < 9 ; ++i ) {
    if ( std::abs(check[i]-zeff[i]) > 1e-3 ) {
      std::cerr << "INPUT " << std::endl;
      print(zeff);
      std::cerr << "OUTPUT" << std::endl;
      print(check);
      throw EXCEPTION("Setting Zeff failed",ERRWAR);
    }
  }

}

void Ddb::setEpsInf(const geometry::mat3d &epsinf) {
  using namespace geometry;
  const vec3d qpt = {{0,0,0}};
  geometry::mat3d d2red(epsinf);

  std::vector<Ddb::d2der>& block = this->getD2der(qpt);

  for ( unsigned idir = 1 ; idir <= 3 ; ++idir )
    d2red[mat3dind( idir, idir)] -= 1.0;

  mat3d gprimTranspose = transpose(_gprim);
  double volume = det(_rprim);

  d2red = (gprimTranspose * (d2red * _gprim))*(-phys::pi*volume);

  for ( unsigned i = 0 ; i < 3 ; ++i ) {
    for ( unsigned j = 0 ; j < 3 ; ++j ) {
      block.push_back(
          std::make_pair(
            std::array<unsigned,4>{{ j, _natom+1, i, _natom+1}},
            complex(d2red[mat3dind(j+1,i+1)],0)
            )
          );
    }
  }
  auto check = this->getEpsInf();
  for ( unsigned i = 0 ; i < 9 ; ++i ) {
    if ( std::abs(check[i]-epsinf[i]) > 1e-3 ) {
      std::cerr << "INPUT " << std::endl;
      print(epsinf);
      std::cerr << "OUTPUT" << std::endl;
      print(check);
      throw EXCEPTION("Setting Eps Inf failed", ERRWAR);
    }
  }
}
