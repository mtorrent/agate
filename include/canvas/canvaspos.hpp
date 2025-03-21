/**
 * @file include/canvaspos.hpp
 *
 * @brief To plot a frame of the crystal with the atomic positions.
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


#ifndef CANVASPOS_HPP
#define CANVASPOS_HPP

#ifdef _WIN32
#include "base/win32.hpp"
#endif

#ifdef HAVE_CONFIG_H
#include "agate.h"
#undef HAVE_CONFIG_H
#endif

#include "canvas/canvas.hpp"
#include "base/geometry.hpp"
#include "shape/octahedra.hpp"
#include "graphism/trisphere.hpp"
#include "graphism/tricylinder.hpp"
#include "graphism/triarrow.hpp"
#include <utility>
#include <typeinfo>
#include <memory>
#ifdef HAVE_GL
# ifdef __APPLE__
#  include <OpenGL/gl.h>
# else
#  include <GL/gl.h>
# endif
#endif

/** 
 * Plot the atomic position and eventually some basic geometry polygon.
 * Currently only octahedra can be plotted.
 */
class CanvasPos : public Canvas {

  protected :

    typedef std::pair<int, geometry::vec3d> indVec3d; ///< Define simple type to store atom id and position.
#ifndef HAVE_GL
    typedef float GLfloat; ///< Overload GLfloat if not defined
#endif

    int                           _natom;      ///< Number of atom to render.
    int                           _ntypat;     ///< Number of type of atoms.
    std::vector<int>              _typat;      ///< Type of each atom to render 
    std::vector<int>              _znucl;      ///< atomic number of each type of atom.
    std::vector<indVec3d>         _onBorders;  ///< atom id and shift to apply w/r to xcart to get the new atom (from the original one)
    std::vector<double>           _xcartBorders; ///< xcart coordiates for atoms on border
    std::vector<int>              _octahedra_z;///< Type of atom to draw octahedra
    std::vector<std::unique_ptr<Octahedra>>   _octahedra;  ///< List of octahedra to draw (should be a generic unique_ptr<polygon*> to be able to draw something else.
    bool                          _hasTranslations; ///< True if rprimd is available for translations.
    unsigned int                  _display;    ///< What labels to display on screen
    bool                          _drawSpins[5];

  private :

    double                        _bond;       ///< Length to draw H bond.
    double                        _bondRadius; ///< Radius of the cylinder for bonds.
    TriSphere*                    _sphere;     ///< Sphere to draw atoms
    TriCylinder                   _cylinder;   ///< Cylinder to draw bonds.
    TriArrow                      _arrow;      ///< Arrow to draw spins or displacement.
    float                         _up[3];      ///< Color spin up
    float                         _down[3];    ///< Color spin down
    float                         _octacolor[4];///< Color spin down
    bool                          _octaDrawAtoms;///< Draw the additional atoms at tops of octahedra
    float                         _maxDim;      ///< max length of the system. Used to adjust view in window for instance.
    double                        _forceFactor; ///< Scaling factor to visualise forces


  protected :

    /**
     * Draw an atom with the TriSphere and atomic number at x,y,z
     * @param znucl Atomic number to get the radius and color
     * @param posX Cartesian x coordinate
     * @param posY Cartesian y coordinate
     * @param posZ Cartesian z coordinate
     */
    void drawAtom(const int znucl, GLfloat posX, GLfloat posY, GLfloat posZ);

    /**
     * Draw the primitive cell made by x,y,z
     */
    void drawCell();

    /**
     * Draw the primitive cell made by x,y,z
     */
    void drawCell(const double *);

    /**
     * Draw the bond
     * @param bonds A vector contains the bonded atoms
     */
    void drawBonds(std::vector< std::pair<int,int> > &bonds);

    /**
     * Draw the Spin arrows if spins are available
     * @param batom element of _onBorders to consider to draw the spin.
     */
    void drawSpins(unsigned batom = -1);

    /**
     * Draw the force arrows if fcarts are available and display has DISP_FORCE
     * @param batom element of _onBorders to consider to draw the force.
     */
    void drawForces(unsigned batom = -1);

    /**
     * Simple function to clean all the data and start from fresh
     */
    virtual void clear();

    /**
     * Alter/modify a canvas with respect to a keyword and stream containing data.
     * @param token keyword to know what to do
     * @param stream the stream with the other data to be parsed.
     */
    void my_alter(std::string token, std::istringstream &stream);

    /**
     * Build the cartesian position of the atoms on the borders
     * @param itime Build for this time step
     * @param findBorders True if the border is searched
     */
    virtual void buildBorders(unsigned itime, bool findBorders);

    /**
     * Look for all bondings.
     * @result A vector of pair giving the atoms to bond.
     * If the atom id is larger than _natom then it means it is on the border.
     */
    virtual std::vector<std::pair<int,int>> buildBonds();

  public :

    static const unsigned                DISP_ZNUCL = 1 << 0; ///< Parameter to construct _display : Here display the atomic numbers
    static const unsigned                DISP_ID    = 1 << 1; ///< Parameter to construct _display : Here display the atomic id (wrt input file)
    static const unsigned                DISP_NAME  = 1 << 2; ///< Parameter to construct _display : Here display the atomic name (wrt input file)
    static const unsigned                DISP_BORDER= 1 << 3; ///< Parameter to construct _display : Here display the atomic name (wrt input file)
    static const unsigned                DISP_BOND  = 1 << 4; ///< Parameter to construct _display : Here display the atomic name (wrt input file)
    static const unsigned                DISP_ATOM  = 1 << 5; ///< Parameter to construct _display : Here display the atomic name (wrt input file)
    static const unsigned                DISP_INCIRCLE= 1 << 6; ///< Parameter to construct _display : Here display the incircle of the lattice only
    static const unsigned                DISP_CELL= 1 << 7; ///< Parameter to construct _display : Here display the cell
    static const unsigned                DISP_FORCE = 1 << 8; ///< Parameter to construct _display : Here display the cell

    /**
     * Constructor.
     */
    CanvasPos(bool drawing);

    /**
     * Copy constructor.
     */
    CanvasPos(const CanvasPos&) = delete;

    /**
     * Move constructor.
     */
    CanvasPos(CanvasPos&&);

    /**
     * Copy operator
     */
    CanvasPos& operator= (const CanvasPos&) = delete;

    /**
     * Move operator
     */
    CanvasPos& operator= (CanvasPos&&) = delete;

    /**
     * Destructor.
     */
    virtual ~CanvasPos();

    /**
     * Set some data from a histdata
     * @param hist History of some parameters.
     */
    virtual void setHist(HistData& hist);

    /**
     * Refresh what to see on the screen
     */
    virtual void refresh(const geometry::vec3d &cam, TextRender &render);

    /**
     * Go to next frame to draw
     * @param count The number of frame to go forward
     */
    void nextFrame(const int count=1);

    /**
     * Get the typical dimension of the problem : here the max length of the problem
     * @param reset The dimension will be multiplied by this value, multiplying by -1 will reset the value for a new evaluation. Positive value will just scale the dimension
     * @return  the typical dimension of the problem
     */
    float typicalDim(float reset = 1.f);

    /**
     * Read geometric figures to plot.
     * @param config The parser containing the parameters
     */
    //virtual void setParameters(const ConfigParser& config);

    /**
     * Construct the list of octahedra to draw
     * @param z the znucl to draw octahedra around
     */
    virtual void updateOctahedra(int z);

    /**
     * Get the display information
     * @return Display variable containing one bit for each possibility
     */
    unsigned int getDisplay() const { return _display; }

    /**
     * Get bond info
     * @param rad bond radius for cylinder drawing
     * @param factor factor to look for neighbours
     */
    void getBondInfo(double& rad, double& factor) const;

    /**
     * Get spin display orientation
     * @param x true if x direction is displayed
     * @param y true if y direction is displayed
     * @param z true if z direction is displayed
     * @param relative true if length is relative, false if absolute
     * @param centered true if arrow is centered in the atom otherwise the arrow starts at the center of the atom
     */
    void getSpinDirection(bool &x, bool &y, bool &z, bool &relative, bool &centered) const;

    /**
     * Get the list of znucl around which an octahedra will be displayed
     * @param drawAtoms flag is set to true if atoms for octahedra are plotted
     * @return a vector of znucl around which an octahedra will be calculated
     */
    std::vector<int> getOctahedra(bool& drawAtoms) const;

    /**
     * Get the base color for plotting octahedra
     * @return RGBA all between 0 and 1
     */
    const float* getOctahedraColor() const;

    /**
     * Display help message with all command that are use in this class
     * @param out the stream to write the help message
     */
    static void help(std::ostream &out);

    /**
     * @brief getForceFactor Get the factor to scale forces when displayed
     * @return The number forces are multiplied by
     */
    double getForceFactor() const;

    /**
     * @brief setForceFactor Set the factor to scale forces when displayed
     * @param forceFactor the number forces are multiplied by
     */
    void setForceFactor(double forceFactor);
};

#endif  // CANVASPOS_HPP
