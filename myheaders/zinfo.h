#ifndef ZINFO_H
#define ZINFO_H

#include <iostream>
#include <ostream>
#include <cmath>
#include <map>


template<class T>
bool sort_Zlist(const T A, T B){ return (A.z < B.z); }



/*!
 * \brief The Zinfo class contains information regarding the z elevation of a
 * mesh node and how this node is  connected in the mesh.
 */

class Zinfo{
public:
    /*!
     * \brief Zinfo construct a new z vertex.
     * Although the ids should not be negative we allow to create Zinfo points with negative ids.
     * However threre sould always be a check before calling this function if the point is going to be
     * added to the mesh structure.
     * \param z is the elevation
     * \param dof is the dof
     * \param level is the level of the node
     * \param constr is true if its a hanging node
     */
    Zinfo(double z, int dof, int level, bool constr, std::map<int,std::pair<int,int> > dof_conn);

    //! This is a map that holds the dofs of the triangulation points as key,
    //! and the level,hangin flag as pair of integers of the points that this is connected with.
    std::map<int,std::pair<int,int> > dof_conn;

    //! prints all the information of this vertex
    void print_me(std::ostream& stream);

    /*!
     * \brief is_same_z compares the elevation of this point with another elevation
     * \param z is the elevation to compare with
     * \param thres When the two elevations are smaller than the threshold are considered equal
     * \return returns true if the elevations are equal
     */
    bool compare(double z, double thres);

    //! Attempts to add connection to this point. If the connection already exists
    //! nothing is added
    void Add_connections(std::map<int,std::pair<int,int> > conn);

    //! You should call this only after this point has at least z info and level assigned
    //! from a previous iteration.
    //! A typical case would be after resetting the mesh structure
    void update_main_info(Zinfo newZ);

    //! This method returns true if the point is connected to this one
    //! Essentially is considered connected if the point in question can be found
    //! in the #dof_conn map of connected nodes.
    //! In practice it appears that a particular node maybe connected with one node
    //! in one cell and not connected in an another cell if the cells that share the node
    //! have different level. However we really need this information only for the hanging
    //! nodes where that never happens,
    bool connected_with(int dof_in);

//    //! Copies the zinfo of the vertex to this vertex. The operation does that blindly
//    //! without checking if the input values make sense.
    //void copy(Zinfo zinfo);

    //! change all values to dummy ones (negative) except the elevation and the level
    void reset();

    //! This is the elevation
    double z;

    //! This is the relative position with respect to the nodes above and below
    double rel_pos;

    //! This is the index of the dof number
    int dof;

    //! This is the level of the vertex
    int level;

    //! This is set to 1 if the node is hanging
    int hanging;

    //! This is the dof of the node above this node
    int id_above;

    //! This is the dof of the node below this node
    int id_below;

    //! The dof of the node that serves as top for this node
    int dof_top;

    //! This is the index of the #dof_top node in the list of the #PntsInfo::Zlist
    int id_top;

    //! The dof of the node that serves as bottom for this node
    int dof_bot;

    //! This is the index of the #dof_bot node in the list of the #PntsInfo::Zlist
    int id_bot;

    //! This is a flag that specifies whether the vertex is used or not
    bool used;
    //! A flag that is set to true if this node is connected with the node above
    //! If this is a hanging node then one of the #connected_above or #connected_below
    //! must be false and the other true
    bool connected_above;

    //! A flag that is set to true if this node is connected with the node below
    //! If this is a hanging node then one of the #connected_above or #connected_below
    //! must be false and the other true
    bool connected_below;

    bool isZset;
};

Zinfo::Zinfo(double z_in, int dof_in, int level_in, bool constr, std::map<int,std::pair<int,int> > conn){
    // To construct a new point we need to know the elevation,
    // the dof, the level and whether is a hanging node.
    // Although the ids should not be negative we allow to create Zinfo points with negative ids
    // However threr sould always be a check before calling this function
    //if (dof_in <0)
    //    std::cerr << "The dof id cannot be negative" << std::endl;
    //if (level_in <0)
    //    std::cerr << "The level id cannot be negative" << std::endl;

    z = z_in;
    dof = dof_in;
    level = level_in;
    if (constr) hanging = 1;
    else hanging = 0;

    id_above = -9;
    id_below = -9;
    used = true;

    dof_top = -9;
    dof_bot= -9;
    id_top = -9;
    id_bot = 9;
    rel_pos = -9.0;
    connected_above = false;
    connected_below = false;
    isZset = false;


    Add_connections(conn);
}

void Zinfo::update_main_info(Zinfo newZ){
    if (newZ.dof <0)
        std::cerr << "The new dof id cannot be negative" << std::endl;
    if (dof >= 0){
        if (dof != newZ.dof){
            std::cerr << " You attempt to update on a point that has already dof\n"
                      <<  "However the updated dof is different from the current dof" << std::endl;
        }
    }
    dof = newZ.dof;
    hanging = newZ.hanging;
    used = newZ.used;
    Add_connections(newZ.dof_conn);
}

void Zinfo::Add_connections(std::map<int,std::pair<int,int> > conn){
    std::map<int,std::pair<int,int> >::iterator it;
    for (it = conn.begin(); it != conn.end(); ++it)
        dof_conn.insert(std::pair<int,std::pair<int,int>>(it->first, it->second));
}


bool Zinfo::compare(double z_in, double thres){
    return (std::abs(z_in - z) < thres);
}

//void Zinfo::copy(Zinfo zinfo){
//    dof         =   zinfo.dof;
//    level       =   zinfo.level;
//    hanging     =   zinfo.hanging;
//    id_above    =   zinfo.id_above;
//    id_below    =   zinfo.id_below;

//    used        =   zinfo.used;

//    id_top      =   zinfo.id_top;
//    id_bot      =   zinfo.id_bot;
//    rel_pos     =   zinfo.rel_pos;
//    z           =   zinfo.z;
//}

bool Zinfo::connected_with(int dof_in){
    return dof_conn.count(dof_in) > 0;
}

void Zinfo::reset(){
    // when we reset a point we change all values to dummy ones except
    // the elevation and the level
    dof = -9;
    hanging = -9;
    id_above = -9;
    id_below = -9;

    used = false;
    isZset = false;

    dof_top = -9;
    dof_bot= -9;
    id_top = -9;
    id_bot = -9;
    rel_pos = -9.0;
    connected_above = false;
    connected_below = false;
    dof_conn.clear();
}


#endif // ZINFO_H
