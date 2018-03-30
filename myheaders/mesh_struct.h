#ifndef MESH_STRUCT_H
#define MESH_STRUCT_H

#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/mapping_q1.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/lac/trilinos_vector.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/distributed/solution_transfer.h>
#include <deal.II/base/conditional_ostream.h>

#include <algorithm>

#include "zinfo.h"
#include "pnt_info.h"
#include "cgal_functions.h"
#include "my_functions.h"
#include "mpi_help.h"
#include "helper_functions.h"
#include "polygon_outline.h"

//! custom struct to hold the rquired information of a triangulation point
template <int dim>
struct trianode {
    Point <dim> pnt;
    int dof;
    int hang;
    int spi; // support_point_index
    std::map<int,int> c_pnt;// dofs of points connected to that node
    int isTop;
    int isBot;
    bool islocal;
    std::vector<types::global_dof_index> cnstr_nd;
};

struct new_DOFZ{
    new_DOFZ(){
        new_dof = -9;
        proc = -9;
        z = -9999.0;
    }
    int new_dof;
    int proc;
    double z;
};

//! Returns true if any neighbor element is ghost
template <int dim>
bool any_ghost_neighbor(typename DoFHandler<dim>::active_cell_iterator cell){
    bool out = false;
    for (unsigned int iface = 0; iface < GeometryInfo<dim>::faces_per_cell; ++iface){
        if (cell->at_boundary(iface)) continue;

        if (cell->neighbor(iface)->active()){
            if (cell->neighbor(iface)->is_ghost()){
                out = true;
                break;
            }
        }
        else{
            for (unsigned int ichild = 0; ichild < cell->face(iface)->n_children();  ++ichild){
                if (cell->neighbor_child_on_subface(iface,ichild)->is_ghost()){
                    out = true;
                    break;
                }
            }
            if (out)
                break;
        }
    }
    return out;
}

/*!
 * \brief The Mesh_struct class contains the coordinates of the entire mesh grouped into lists of dim-1 points,
 * where each point contains a list of the z node elevation.
 */
template <int dim>
class Mesh_struct{
public:

    /*!
     * \brief Mesh_struct The constructor just sets the threshold values
     * \param xy_thr this is the threshold for the x-y coordinates. Two point with distance smaller that xy_thr
     * are considered identical. Typically the xy threshold has larger values compared to the z threshold.
     * \param z_thr this is the treshold for the z coordinates. Two nodes with the same x-y coordinates
     * are considered identical if their elevation is smaller that the z_thr
     */
    Mesh_struct(double xy_thr, double z_thr);

    //! The threshold along the x-y coordinates
    double xy_thres;
    //! The threshold along the z coordinates
    double z_thres;

    //! This is a counter for the points in the #PointsMap
    int _counter;

    //! This map associates each point with a unique id (#_counter)
    std::map<int , PntsInfo<dim> > PointsMap;

    //! This is a Map structure that relates the dofs with the PointsMap.
    //! The key is the dof and the value is the pair #PointsMap key and the index of the z value in
    //! the Zlist of the #PointsMap.
    //! In other words <dof> - <xy_index, z_index>
    std::map<int,std::pair<int,int> > dof_ij;

    //! this is a cgal container of the points of this class stored in an optimized way for spatial queries
    PointSet2 CGALset;

    //! Adds a new point in the structure. If the point exists adds the z coordinate only and returns
    //! the id of the existing point. if the point doesnt exist creates a new point and returns the new id.
    void add_new_point(Point<dim-1>, Zinfo zinfo);

    //! Checks if the point already exists in the mesh structure
    //! If the point exists it returns the id of the point in the #CGALset
    //! otherwise returns -9;
    int check_if_point_exists(Point<dim-1> p);

    /*!
     * \brief updateMeshstruct is the heart of this class. For a given parallel triangulation updates the existing
     * points or creates new ones.
     *
     * The method first loops through the locally owned cells and extracts the coordinates and dof for each node
     * which stores it to #distributed_mesh_vertices. Then keeps in a custom map information for each node of the
     * triangulation such as : dof, level (level is set only once when the node is first created), whether is a hanging
     * node, and a list of connections with othe nodes, where for each connection the dof, level and hanging information is also stored.
     * NOTE: the connected nodes may be more than what they actually are:
     *
     *          a       d
     * -----------------
     * |___|___|c      |
     * |   |   |       |
     * -----------------
     *         b        e
     *
     *
     * In the example above node a would appear to have connections with d b and c. While this is not correct doesnt seem to
     * influence the algorithm because the hanging nodes have always the correct number of connections
     *
     * \param xy_thr this is the threshold for the x-y coordinates. Two point with distance smaller that xy_thr
     * are considered identical. Typically the xy threshold has larger values compared to the z threshold.
     * \param distributed_mesh_vertices is a vector of size #dim x (Number of triangulation vertices).
     * Essentially we treat all the coordinates as unknowns yet only the vertical component is the one we are going
     * to change
     */
    void updateMeshStruct(DoFHandler<dim>& mesh_dof_handler,
                         FESystem<dim>& mesh_fe,
                         ConstraintMatrix& mesh_constraints,
                         IndexSet& mesh_locally_owned,
                         IndexSet& mesh_locally_relevant,
                         TrilinosWrappers::MPI::Vector& mesh_vertices,
                         TrilinosWrappers::MPI::Vector& distributed_mesh_vertices,
                         TrilinosWrappers::MPI::Vector& mesh_Offset_vertices,
                         TrilinosWrappers::MPI::Vector& distributed_mesh_Offset_vertices,
                         MPI_Comm&  mpi_communicator,
                         ConditionalOStream pcout,
                         std::string prefix);

    //! Once the #PointsMap::T and #PointsMap::B have been set to a new elevation and have also
    //! the relative positions calculated we can use this method to update the z elevations of the
    //! Mesh structure. Then the updated elevations will be transfered to the mesh.
    //! The update starts with the nodes at level 0, which can be set directly as they do not depend on
    //! any other node.
    void updateMeshElevation(DoFHandler<dim>& mesh_dof_handler,
                             parallel::distributed::Triangulation<dim> &triangulation,
                             ConstraintMatrix& mesh_constraints,
                             TrilinosWrappers::MPI::Vector& mesh_vertices,
                             TrilinosWrappers::MPI::Vector& distributed_mesh_vertices,
                             TrilinosWrappers::MPI::Vector& mesh_Offset_vertices,
                             TrilinosWrappers::MPI::Vector& distributed_mesh_Offset_vertices,
                             MPI_Comm&  mpi_communicator,
                             ConditionalOStream pcout,
                             std::string prefix);

    //! resets all the information that is contained except the coordinates and the level of the points
    void reset();

    //! Prints to screen the number of vertices the #myrank processor has.
    //! It is used primarily for debuging
    void n_vertices(int myrank);

    //! Assuming that all processors have gathered all Z nodes for each xy point they own
    //! this routine identifies the dofs above and below each node, how the nodes are connected,
    //! and sets the top and bottom elevation for each xy point
    //! (MAYBE THIS SHOULD SET THE DOF of the top/bottom node and not the elevation
    void set_id_above_below(int my_rank);

    //! This creates the #dof_ij map.
    void make_dof_ij_map();

    //! This method calculates the top and bottom elevation on the points of the #PointsMap
    //! This should be called on the initial grid before any refinement
    void compute_initial_elevations(MyFunction<dim, dim-1> top_function,
                                    MyFunction<dim, dim-1> bot_function,
                                    std::vector<double>& vert_discr);

    //! This method sets the scales #dbg_scale_x and #dbg_scale_z for debug plotting using softwares like houdini
    void dbg_set_scales(double xscale, double zscale);


    void move_vertices(DoFHandler<dim>& mesh_dof_handler,
                       TrilinosWrappers::MPI::Vector& mesh_vertices,
                       unsigned int my_rank,
                       std::string prefix);

    void printMesh(std::string filename, unsigned int i_proc, DoFHandler<dim>& mesh_dof_handler);
private:
    void dbg_meshStructInfo2D(std::string filename, unsigned int n_proc);
    void dbg_meshStructInfo3D(std::string filename, unsigned int n_proc);
    double dbg_scale_x;
    double dbg_scale_z;

    void identify_local_connections();
    void identify_dependencies();

};

template <int dim>
Mesh_struct<dim>::Mesh_struct(double xy_thr, double z_thr){
    xy_thres = xy_thr;
    z_thres = z_thr;
    _counter = 0;
    dbg_scale_x = 100;
    dbg_scale_z = 10;
}

template <int dim>
void Mesh_struct<dim>::add_new_point(Point<dim-1>p, Zinfo zinfo){

    //if (zinfo.dof == 189)
    //    std::cout << "SO FAR SO GOOD" << std::endl;

    //if (zinfo.dof < 0)
    //    std::cerr << "You attepmt to add a point with negative dof" << std::endl << std::flush;

    // First search for the XY location in the structure
    int id = check_if_point_exists(p);

    //if (zinfo.dof == 189)
    //    std::cout << "id is " << id << std::endl;

    if ( id < 0 ){
        // this is a new point and we add it to the map
        PntsInfo<dim> tempPnt(p, zinfo);
        tempPnt.find_id = _counter;
        PointsMap[_counter] = tempPnt;

        //... to the Cgal structure
        std::vector< std::pair<ine_Point2,unsigned> > pair_point_id;
        double x,y;
        if (dim == 2){
            x = p[0];
            y = 0;
        }else if (dim == 3){
            x = p[0];
            y = p[1];
        }
        pair_point_id.push_back(std::make_pair(ine_Point2(x, y), _counter));
        CGALset.insert(pair_point_id.begin(), pair_point_id.end());
        _counter++;
    }else if (id >=0){
        typename std::map<int, PntsInfo<dim> >::iterator it = PointsMap.find(id);
        //if (zinfo.dof == 189){
        //    if (it == PointsMap.end())
        //        std::cout << "NO WAY" << std::endl;
        //    else
        //        std::cout << it->second.PNT[0] << std::endl;
        //}
        it->second.add_Zcoord(zinfo, z_thres);
    }
}

template <int dim>
int Mesh_struct<dim>::check_if_point_exists(Point<dim-1> p){
    int out = -9;
    double x,y;
    if (dim == 2){
        x = p[0];
        y = 0;
    }else if (dim == 3){
        x = p[0];
        y = p[1];
    }

    std::vector<int> ids = circle_search_in_2DSet(CGALset, ine_Point3(x, y, 0.0) , xy_thres);

    if (ids.size() > 1)
        std::cerr << "More than one points around x: " << x << ", y: " << y << "found within the specified tolerance" << std::endl;
    else if(ids.size() == 1) {
         out = ids[0];
    }
    else{
         out = -9;
    }

    return out;
}


template <int dim>
void Mesh_struct<dim>::updateMeshStruct(DoFHandler<dim>& mesh_dof_handler,
                                       FESystem<dim>& mesh_fe,
                                       ConstraintMatrix& mesh_constraints,
                                       IndexSet& mesh_locally_owned,
                                       IndexSet& mesh_locally_relevant,
                                       TrilinosWrappers::MPI::Vector& mesh_vertices,
                                       TrilinosWrappers::MPI::Vector& distributed_mesh_vertices,
                                       TrilinosWrappers::MPI::Vector& mesh_Offset_vertices,
                                       TrilinosWrappers::MPI::Vector& distributed_mesh_Offset_vertices,
                                       MPI_Comm&  mpi_communicator,
                                       ConditionalOStream pcout,
                                       std::string prefix){
    // Use this to time the operation. Note that this is a very expensive operation but nessecary
    std::clock_t begin_t = std::clock();
    // get the rank and processor id just for output display
    unsigned int my_rank = Utilities::MPI::this_mpi_process(mpi_communicator);
    unsigned int n_proc = Utilities::MPI::n_mpi_processes(mpi_communicator);

    // make sure all processors start together
    MPI_Barrier(mpi_communicator);
    reset(); // delete all info in the Mesh structure
    MPI_Barrier(mpi_communicator);

    const MappingQ1<dim> mapping;

    pcout << "Distribute mesh dofs..." << mesh_dof_handler.n_dofs() << std::endl << std::flush;

    mesh_dof_handler.distribute_dofs(mesh_fe);
    pcout << "dofs :" << mesh_dof_handler.n_dofs() << std::endl << std::flush;
    mesh_locally_owned = mesh_dof_handler.locally_owned_dofs();
    DoFTools::extract_locally_relevant_dofs (mesh_dof_handler, mesh_locally_relevant);
    mesh_vertices.reinit (mesh_locally_owned, mesh_locally_relevant, mpi_communicator);
    distributed_mesh_vertices.reinit(mesh_locally_owned, mpi_communicator);
    mesh_Offset_vertices.reinit (mesh_locally_owned, mesh_locally_relevant, mpi_communicator);
    distributed_mesh_Offset_vertices.reinit(mesh_locally_owned, mpi_communicator);

    const std::vector<Point<dim> > mesh_support_points
                                  = mesh_fe.base_element(0).get_unit_support_points();

    FEValues<dim> fe_mesh_points (mapping,
                                  mesh_fe,
                                  mesh_support_points,
                                  update_quadrature_points);

    mesh_constraints.clear();
    mesh_constraints.reinit(mesh_locally_relevant);
    DoFTools::make_hanging_node_constraints(mesh_dof_handler, mesh_constraints);
    mesh_constraints.close();

    // to avoid duplicate executions we will maintain a map with the dofs that have been
    // already processed
    std::map<int,int>::iterator itint;
    MPI_Barrier(mpi_communicator);

    // Make a list of points in x-y that
    std::vector<std::vector<Point<dim-1> > > pointsXY(n_proc);
    std::vector<std::vector<ine_Point2 > > pointsXYcgal(n_proc);

    pcout << "Update XYZ structure...for: " << prefix  << std::endl << std::flush;
    std::vector<unsigned int> cell_dof_indices (mesh_fe.dofs_per_cell);
    typename DoFHandler<dim>::active_cell_iterator
    cell = mesh_dof_handler.begin_active(),
    endc = mesh_dof_handler.end();
    MPI_Barrier(mpi_communicator);
    //int dbg_cnt =0;
    for (; cell != endc; ++cell){ // We will loop through the locally owned and ghost cells
        if (cell->is_locally_owned() || cell->is_ghost()){
            bool top_cell = false;
            bool bot_cell = false;
            // If the neighbor index of the top or bottom face of the cell is negative
            // then this cell is either top or bottom.
            if (cell->neighbor_index(GeometryInfo<dim>::faces_per_cell-2) < 0){
                bot_cell = true;
            }
            if (cell->neighbor_index(GeometryInfo<dim>::faces_per_cell-1) < 0){
                top_cell = true;
            }

            fe_mesh_points.reinit(cell);
            cell->get_dof_indices (cell_dof_indices);
            // First we will loop through the cell dofs gathering all info we need for the points
            // and then we will loop again though the points to add them into the structure.
            // Therefore we would need to initialize several vectors
            std::map<int, trianode<dim> > curr_cell_info;


            for (unsigned int idof = 0; idof < mesh_fe.base_element(0).dofs_per_cell; ++idof){
                // for each dof of this cell we extract the coordinates and the dofs
                Point <dim> current_node;
                std::vector<int> current_dofs(dim);
                std::vector<unsigned int> spi;
                for (unsigned int dir = 0; dir < dim; ++dir){
                    // for each cell, the support_point_index spans from 0 to dim*Nvert_per_cell-1
                    // eg for dim =2 spans from 0-7
                    // The first dim indices correspond to x,y,z of the first vertex of triangulation
                    // The current_dofs contains the dof index for each coordinate.
                    // The current_node containts the x,y,z coordinates
                    // The distributed_mesh_vertices is a vector of size Nvertices*dim
                    // essentially we are treating all xyz coordinates as variables although we are going to
                    // change only the vertical component of it (In 2D this is the y).
                    unsigned int support_point_index = mesh_fe.component_to_system_index(dir, idof );
                    spi.push_back(support_point_index);
                    current_dofs[dir] = static_cast<int>(cell_dof_indices[support_point_index]);
                    current_node[dir] = fe_mesh_points.quadrature_point(idof)[dir];
                    distributed_mesh_vertices[cell_dof_indices[support_point_index]] = current_node[dir];

                    //if (!distributed_mesh_vertices.in_local_range(cell_dof_indices[support_point_index]) && my_rank == 0){
                    //    pcout << "dir:" << dir << ", idof:" << idof << ", cur_dof:" << current_dofs[dir]
                    //          <<   ", cur_nd:" << current_node[dir] << ", spi:" << support_point_index << std::endl;
                    //}

                }
                // We have now loop throught dofs of a given cell point and we initialize a trianode
                trianode<dim> temp;
                temp.pnt = current_node;
                temp.dof = current_dofs[dim-1];
                temp.hang = mesh_constraints.is_constrained(current_dofs[dim-1]);
                temp.cnstr_nd.push_back(current_dofs[dim-1]);
                mesh_constraints.resolve_indices(temp.cnstr_nd);
                temp.spi = spi[dim-1];
                temp.islocal = distributed_mesh_vertices.in_local_range(temp.dof);
                temp.isBot = 0;
                temp.isTop = 0;
                if (bot_cell){
                    if (idof < GeometryInfo<dim>::vertices_per_cell/2){
                        temp.isBot = 1;
                        //std::cout << "bottom point" << std::endl;
                    }
                }
                if (top_cell){
                    if (idof >= GeometryInfo<dim>::vertices_per_cell/2){
                        temp.isTop = 1;
                        //std::cout << "top point" << std::endl;
                    }
                }
                // and last we add it to the map
                curr_cell_info[idof] = temp;
            }

            typename std::map<int, trianode<dim> >::iterator it;
            for (it = curr_cell_info.begin(); it != curr_cell_info.end(); ++it){

                // get the nodes connected with this one
                std::vector<int> id_conn = get_connected_indices<dim>(it->first);

                // create a vector of the points conected with this one
                std::vector<int> connectedNodes;
                for (unsigned int i = 0; i < id_conn.size(); ++i){
                    connectedNodes.push_back(curr_cell_info[id_conn[i]].dof);
                }

                // create a vector of ints to hold the nodes that this node depends on if its constrained
                std::vector<int> temp_cnstr;
                for (unsigned int ii = 0; ii < it->second.cnstr_nd.size(); ++ii){
                    temp_cnstr.push_back(it->second.cnstr_nd[ii]);
                }

                // Now create a zinfo variable
                Zinfo zinfo(it->second.pnt[dim-1], it->second.dof, temp_cnstr, it->second.isTop, it->second.isBot, connectedNodes);
                zinfo.is_local = it->second.islocal;

                // and a point
                Point<dim-1> ptemp;
                for (unsigned int d = 0; d < dim-1; ++d)
                    ptemp[d] = it->second.pnt[d];

                add_new_point(ptemp, zinfo);
            }
        }
    }

    make_dof_ij_map();
    set_id_above_below(my_rank);
    MPI_Barrier(mpi_communicator);

    dbg_meshStructInfo3D("Test01_" + prefix + "_", my_rank);


    // in multi processor simulations more than likely there would be nodes that have as top or bottom information
    // that lives in another processor. The following code takes care of that.
    if (n_proc > 1){
        // We will maintain two maps to store the nodes that each processor will ask information from other processors
        std::map<int, new_DOFZ> Top_info;
        std::map<int, new_DOFZ> Bot_info;

        // And define few standard iterators
        typename std::map<int ,  PntsInfo<dim> >::iterator it;
        std::vector<Zinfo>::iterator itz;
        std::map<int,std::pair<int,int>>::iterator it_dof;

        // The following loop is executed as long as a processor has unknown nodes in its local dofs only
        // Each processor contains non local dofs but for those their information is not correct other than
        // they exists in the triangulation. However their connection information is correct
        while (true){
            pcout << "--------------" << std::endl;
            Top_info.clear();
            Bot_info.clear();

            // gather the unknown dofs from each processor.
            for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
                for (itz = it->second.Zlist.begin(); itz != it->second.Zlist.end(); ++itz){
                    if (itz->is_local){
                        if (itz->Bot.proc < 0){ // we do not know anything about the bottom if we dont know which processor owns the bottom node
                            Bot_info.insert(std::pair<int,new_DOFZ>(itz->Bot.dof, new_DOFZ()));
                        }
                        if (itz->Top.proc < 0){ // we do not know anything about the top if we dont know which processor owns this node
                            Top_info.insert(std::pair<int,new_DOFZ>(itz->Top.dof, new_DOFZ()));
                        }
                    }
                }
            }


            // Check if there are any nodes to be set. If not the break the loop
            std::vector<int> top_info_size;
            std::vector<int> bot_info_size;
            Send_receive_size(static_cast<unsigned int>(Top_info.size()), n_proc, top_info_size, mpi_communicator);
            Send_receive_size(static_cast<unsigned int>(Bot_info.size()), n_proc, bot_info_size, mpi_communicator);
            int temp_count = 0;
            for (unsigned int i = 0; i < n_proc; ++i){
                temp_count = temp_count + top_info_size[i] + bot_info_size[i];
            }
            if (temp_count == 0)
                break;


            MPI_Barrier(mpi_communicator);
            std::cout << "Proc " << my_rank << " has " << Bot_info.size() << ", " << Top_info.size() << "Bot/Top" << std::endl;
            //if (my_rank == 2 && Top_info.size() == 1){
            //    for (unsigned int jj = 0; jj < Top_info.size(); ++jj){
            //        std::map<int,new_DOFZ>::iterator itd = Top_info.begin();
                    //std::cout << "Rank " << my_rank << " Top not set " << itd->first << std::endl;
            //    }
            //}


            // initialize and populate the vectors to be sent
            std::vector<std::vector<int>> top_send(n_proc);
            std::vector<std::vector<int>> bot_send(n_proc);
            std::vector<int> top_size_send;
            std::vector<int> bot_size_send;
            for (std::map<int,new_DOFZ>::iterator itd = Top_info.begin(); itd != Top_info.end(); ++itd){
                top_send[my_rank].push_back(itd->first);
            }
            for (std::map<int,new_DOFZ>::iterator itd = Bot_info.begin(); itd != Bot_info.end(); ++itd){
                bot_send[my_rank].push_back(itd->first);
            }
            // Send the unknown top and bottom dofs
            Send_receive_size(static_cast<unsigned int>(top_send[my_rank].size()), n_proc, top_size_send, mpi_communicator);
            Sent_receive_data<int>(top_send, top_size_send, my_rank, mpi_communicator, MPI_INT);
            Send_receive_size(static_cast<unsigned int>(bot_send[my_rank].size()), n_proc, bot_size_send, mpi_communicator);
            Sent_receive_data<int>(bot_send, bot_size_send, my_rank, mpi_communicator, MPI_INT);


            std::vector<std::vector<int>> top_info_proc(n_proc);
            std::vector<std::vector<int>> top_info_dof_ask(n_proc);
            std::vector<std::vector<int>> top_info_new_dof(n_proc);
            std::vector<std::vector<double>> top_z_reply(n_proc);
            std::vector<std::vector<int>> bot_info_reply(n_proc);
            std::vector<std::vector<double>> bot_z_reply(n_proc);
            std::vector<int> send_size;

            // now we will loop through the dofs that the other processors have sent.
            // Although it seems unessecary we have to loop through the points that this
            // processor has sent as well because its not uncommon that after few iterations
            // the actual top/bottom node lives indeed in the same processor.
            for (unsigned int i_proc = 0; i_proc < n_proc; ++i_proc){
                // search for the top
                for (unsigned int i = 0; i < top_send[i_proc].size(); ++i){
                    // each processor check if it contains the requested dof
                    it_dof = dof_ij.find(top_send[i_proc][i]);
                    if (it_dof != dof_ij.end()){
                        // if yes dof_ij tell us the indices in the structure
                        int ipnt = it_dof->second.first;
                        int iz = it_dof->second.second;
                        if (PointsMap[ipnt].Zlist[iz].is_local){
                            //if this node is local in this processor we can safely return its information
                            // we sent
                            // which processor asked for this node
                            // the dof that the processor has as unknown
                            // The dof that this dof has as its top
                            // and the z elevation of the node that has as its top. if the elevation is -9999
                            // then this node will sent false z elevation but this will be taken care in a later iteration
                            top_info_proc[my_rank].push_back(static_cast<int>(i_proc));
                            top_info_dof_ask[my_rank].push_back(top_send[i_proc][i]);
                            top_info_new_dof[my_rank].push_back(PointsMap[ipnt].Zlist[iz].Top.dof);
                            top_z_reply[my_rank].push_back(PointsMap[ipnt].Zlist[iz].Top.z);
                        }
                    }
                }

                // In a similar way we search for the bottom
                for (unsigned int i = 0; i < bot_send[i_proc].size(); ++i){
                    it_dof = dof_ij.find(bot_send[i_proc][i]);
                    if (it_dof != dof_ij.end()){
                        int ipnt = it_dof->second.first;
                        int iz = it_dof->second.second;
                        if (PointsMap[ipnt].Zlist[iz].is_local){
                            bot_info_reply[my_rank].push_back(static_cast<int>(i_proc));
                            bot_info_reply[my_rank].push_back(bot_send[i_proc][i]);
                            bot_info_reply[my_rank].push_back(PointsMap[ipnt].Zlist[iz].Bot.dof);
                            bot_z_reply[my_rank].push_back(PointsMap[ipnt].Zlist[iz].Bot.z);
                        }
                    }
                }
            }

            // During development I tried two transfer schemas. 1) put the info into separate vectors (for the top)
            // and 2) group the tranfers per type (int and double).
            Send_receive_size(static_cast<unsigned int>(top_info_proc[my_rank].size()), n_proc, send_size, mpi_communicator);
            Sent_receive_data<int>(top_info_proc, send_size, my_rank, mpi_communicator, MPI_INT);
            Sent_receive_data<int>(top_info_dof_ask, send_size, my_rank, mpi_communicator, MPI_INT);
            Sent_receive_data<int>(top_info_new_dof, send_size, my_rank, mpi_communicator, MPI_INT);
            Sent_receive_data<double>(top_z_reply, send_size, my_rank, mpi_communicator, MPI_DOUBLE);

            Send_receive_size(static_cast<unsigned int>(bot_info_reply[my_rank].size()), n_proc, send_size, mpi_communicator);
            Sent_receive_data<int>(bot_info_reply, send_size, my_rank, mpi_communicator, MPI_INT);
            Send_receive_size(static_cast<unsigned int>(bot_z_reply[my_rank].size()), n_proc, send_size, mpi_communicator);
            Sent_receive_data<double>(bot_z_reply, send_size, my_rank, mpi_communicator, MPI_DOUBLE);

            //std::cout << "Proc " << my_rank << " has " << top_info_reply[my_rank].size() << ", " << top_z_reply[my_rank].size() << std::endl;


            // Once again the processor will loop through the other processors replies.
            // Same as above each processor should check through its own requests too.
            for (unsigned int i_proc = 0; i_proc < n_proc; ++i_proc){
                for (unsigned int i = 0; i < top_z_reply[i_proc].size(); ++i){
                    // if the processor that has asked for this point is me
                    if (top_info_proc[i_proc][i] == my_rank){
                        // This is the dof that has the unknown top
                        int dof_asked = top_info_dof_ask[i_proc][i];
                        //this is the new top that the other processor suggested
                        int newdof = top_info_new_dof[i_proc][i];
                        // and this is the new z that was suggested by the processor
                        double newz = top_z_reply[i_proc][i];
                        // This should always be true, but we check for it anyway
                        std::map<int, new_DOFZ>::iterator itt = Top_info.find(dof_asked);
                        if (itt != Top_info.end()){
                            // we update the new dof and new z
                            itt->second.new_dof = newdof;
                            itt->second.z = newz;
                            // but we set the processor only if the z is not -9999
                            if (!(std::abs(newz + 9999.0) < 0.00001)){
                                itt->second.proc = static_cast<int>(i_proc);
                            }
                        }
                        else {
                            std::cout << dof_asked << " NOt found" << std::endl;
                        }
                    }
                }

                // Similarly for the bottom.
                for (unsigned int i = 0; i < bot_z_reply[i_proc].size(); ++i){
                    if (bot_info_reply[i_proc][3*i] == my_rank){
                        int dof_asked = bot_info_reply[i_proc][3*i+1];
                        int newdof = bot_info_reply[i_proc][3*i+2];
                        double newz = bot_z_reply[i_proc][i];
                        std::map<int, new_DOFZ>::iterator itt = Bot_info.find(dof_asked);
                        if (itt != Bot_info.end()){
                            itt->second.new_dof = newdof;
                            itt->second.z = newz;
                            if (!(std::abs(newz + 9999.0) < 0.00001)){
                                itt->second.proc = static_cast<int>(i_proc);
                            }
                        }
                    }
                }
            }

            // We have updated the temporary maps. However we need to assign the updates info to the main
            // structure
            for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
                for (itz = it->second.Zlist.begin(); itz != it->second.Zlist.end(); ++itz){
                    if (itz->is_local){
                        if (itz->Bot.proc < 0){
                            std::map<int, new_DOFZ>::iterator itt = Bot_info.find(itz->Bot.dof);
                            if (itt != Bot_info.end()){
                                itz->Bot.dof = itt->second.new_dof;
                                itz->Bot.proc = itt->second.proc;
                                itz->Bot.z = itt->second.z;
                            }
                        }
                        if (itz->Top.proc < 0){
                            std::map<int, new_DOFZ>::iterator itt = Top_info.find(itz->Top.dof);
                            if (itt != Top_info.end()){
                                itz->Top.dof = itt->second.new_dof;
                                itz->Top.proc = itt->second.proc;
                                itz->Top.z = itt->second.z;
                            }
                        }
                    }
                }
            }
            dbg_meshStructInfo3D("Test02_" + prefix + "_", my_rank);
        }
    }

    std::clock_t end_t = std::clock();
    double elapsed_secs = double(end_t - begin_t)/CLOCKS_PER_SEC;
    //std::cout << "====================================================" << std::endl;
    std::cout << "I'm rank " << my_rank << " and spend " << elapsed_secs << " sec on Updating XYZ" << std::endl;
    //std::cout << "====================================================" << std::endl;
    MPI_Barrier(mpi_communicator);
}

template <int dim>
void Mesh_struct<dim>::reset(){
    _counter = 0;
    PointsMap.clear();
    dof_ij.clear();
    CGALset.clear();
}

template <int dim>
void Mesh_struct<dim>::n_vertices(int myrank){
    int Nxy = PointsMap.size();
    int Nz = 0;
    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        Nz += it->second.Zlist.size();
    }
    std::cout << "I'm " << myrank << ", Nxy = " << Nxy << ", Nz = " << Nz << std::endl;
}

template <int dim>
void Mesh_struct<dim>::dbg_meshStructInfo2D(std::string filename, unsigned int my_rank){
    const std::string log_file_name = (filename	+ "_" +
                                       Utilities::int_to_string(my_rank+1, 4) +
                                       ".txt");
     std::ofstream log_file;
     log_file.open(log_file_name.c_str());
     typename std::map<int , PntsInfo<dim> >::iterator it;
     for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
         double x,y,z;
         x = it->second.PNT[0]/dbg_scale_x;
         if (dim == 3) z = it->second.PNT[1]/dbg_scale_x;
         else z = 0;
         y = 0;
         log_file << std::setprecision(3)
                  << std::fixed
                  << std::setw(15) << x << ", "
                  << std::setw(15) << y << ", "
                  << std::setw(15) << z << ", "
                  << std::setw(15) << it->second.T << ", "
                  << std::setw(15) << it->second.B << ", "
                  << std::setw(5) << it->second.have_to_send
                  << std::endl;
     }
     log_file.close();

}

template <int dim>
void Mesh_struct<dim>::dbg_meshStructInfo3D(std::string filename, unsigned int my_rank){
    const std::string log_file_name = (filename + "_pnt_" +
                                       Utilities::int_to_string(my_rank+1, 4) +
                                       ".txt");
     std::ofstream log_file;
     log_file.open(log_file_name.c_str());

     std::map<std::pair<int,int>, int> line_map;
     std::pair<std::map<std::pair<int,int>,int>::iterator,bool> ret;
     int counter = 0;

     typename std::map<int , PntsInfo<dim> >::iterator it;
     for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
         std::vector<Zinfo>::iterator itz = it->second.Zlist.begin();
         for (; itz != it->second.Zlist.end(); ++itz){
             double x,y,z;
             x = it->second.PNT[0]/dbg_scale_x;
             if (dim == 3) z = it->second.PNT[1]/dbg_scale_x;
             else z = 0;
             y = itz->z/dbg_scale_z;
             log_file << std::setprecision(3)
                      << std::fixed
                      << std::setw(15) << x << ", "
                      << std::setw(15) << y << ", "
                      << std::setw(15) << z << ", "
                      << std::setw(15) << itz->dof << ", "
                      << std::setw(15) << itz->is_local << ", "
                      << std::setw(15) << itz->connected_above  << ", "
                      << std::setw(15) << itz->connected_below << ", "
                      << std::setw(15) << itz->Top.dof  << ", "
                      << std::setw(15) << itz->Bot.dof << ", "
                      << std::setw(15) << itz->Top.z  << ", "
                      << std::setw(15) << itz->Bot.z << ", "
                      << std::setw(15) << itz->Top.proc  << ", "
                      << std::setw(15) << itz->Bot.proc << ", "
                      << std::setw(15) << itz->isTop << ", "
                      << std::setw(15) << itz->isBot << ", "
                      << std::setw(15) << itz->dof_conn.size() << ", "
                      << std::setw(15) << itz->cnstr_nds.size() << ", "
                      << std::setw(15) << itz->hanging << ", "
                      << std::setw(15) << itz->rel_pos  << ", "
                      << std::setw(15) << it->second.T << ", "
                      << std::setw(15) << it->second.B << ", "
                      << std::setw(5) << it->second.have_to_send
                      << std::endl;


             std::map<int, int >::iterator itt;
             for (unsigned int i = 0; i < itz->dof_conn.size(); ++i){
                 int a,b;
                 if (itz->dof < itz->dof_conn[i]){
                     a = itz->dof;
                     b = itz->dof_conn[i];
                 }
                 else{
                     a = itz->dof_conn[i];
                     b = itz->dof;
                 }
                 ret = line_map.insert(std::pair<std::pair<int,int>,int>(std::pair<int,int>(a,b),counter));
                 if (ret.second == true)
                    counter++;
             }
         }
     }
     log_file.close();

     // Print the lines hopefully these are unique

//     const std::string log_file_name1 = (filename + "_lns_" +
//                                        Utilities::int_to_string(my_rank+1, 4) +
//                                        ".txt");

//     std::ofstream log_file1;
//     log_file1.open(log_file_name1.c_str());

//     std::map<int,std::pair<int,int> >::iterator it_dof;
//     std::map<std::pair<int,int>, int>::iterator itl;
//     double x1,y1,z1,x2,y2,z2;
//     for (itl = line_map.begin(); itl!=line_map.end(); ++itl){
//        it_dof = dof_ij.find(itl->first.first);
//        if (it_dof != dof_ij.end()){
//            x1 = PointsMap[it_dof->second.first].PNT[0];
//            if (dim ==2 ) z1 = 0; else
//                z1 = PointsMap[it_dof->second.first].PNT[1];
//            y1 = PointsMap[it_dof->second.first].Zlist[it_dof->second.second].z;

//            it_dof = dof_ij.find(itl->first.second);
//            if (it_dof != dof_ij.end()){
//                x2 = PointsMap[it_dof->second.first].PNT[0];
//                if (dim ==2 ) z2 = 0; else
//                    z2 = PointsMap[it_dof->second.first].PNT[1];
//                y2 = PointsMap[it_dof->second.first].Zlist[it_dof->second.second].z;
//                log_file1 << x1/dbg_scale_x << ", " << y1/dbg_scale_z << ", " << z1/dbg_scale_x << ", "
//                          << x2/dbg_scale_x << ", " << y2/dbg_scale_z << ", " << z2/dbg_scale_x <<  std::endl;
//            }
//        }
//     }
//     log_file1.close();
}

template<int dim>
void Mesh_struct<dim>::updateMeshElevation(DoFHandler<dim>& mesh_dof_handler,
                                           parallel::distributed::Triangulation<dim>& 	triangulation,
                                           ConstraintMatrix& mesh_constraints,
                                           TrilinosWrappers::MPI::Vector& mesh_vertices,
                                           TrilinosWrappers::MPI::Vector& distributed_mesh_vertices,
                                           TrilinosWrappers::MPI::Vector& mesh_Offset_vertices,
                                           TrilinosWrappers::MPI::Vector& distributed_mesh_Offset_vertices,
                                           MPI_Comm&  mpi_communicator,
                                           ConditionalOStream pcout,
                                           std::string prefix){
    unsigned int my_rank = Utilities::MPI::this_mpi_process(mpi_communicator);
    unsigned int n_proc = Utilities::MPI::n_mpi_processes(mpi_communicator);

    typename std::map<int , PntsInfo<dim> >::iterator it;
    std::map<int,std::pair<int,int> >::iterator it_ij; // iterator for dof_ij

    //int dbg_iter = 0;


    // It is assumed that the nodes that lay on the top or bottom and they are local have already been
    // assigned with the correct elevation. The relative positions also have been calculated.

    // elev_asked is a map that contains the dof and elevations of nodes that belong to other processors and this
    // processor has asked at some point.
    std::map<int, double> elev_asked;
    int dbg_cnt = 0;
    while (true){
        MPI_Barrier(mpi_communicator);
        pcout << "========= " << dbg_cnt << " =========" << std::endl;

        std::vector<int> top_info_size;
        std::vector<int> bot_info_size;
        std::vector<std::vector<int>> dof_ask(n_proc);
        std::map<int,int> dof_ask_map;
        dof_ask_map.clear();

        int count_not_set = 0;
        for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
            std::vector<Zinfo>::iterator itz = it->second.Zlist.begin();
            for (; itz != it->second.Zlist.end(); ++itz){
                if (itz->is_local){
                    if (!itz->isZset){
                        if (itz->hanging == 1){ //-----------------------IS HANGING-------------------------------
                            // if the node is hanging then compute its new elevation by averaging the
                            // elevations of the nodes that constraint this one. Do the computation only if all the nodes
                            // have been set
                            bool all_known = true;
                            double sum_z = 0;
                            for (unsigned int ii = 0; ii < itz->cnstr_nds.size(); ++ii){
                                // Find if the node exists in the map
                                bool not_local = false;
                                it_ij = dof_ij.find(itz->cnstr_nds[ii]);
                                if (it_ij != dof_ij.end()){// if exists, check if it's local
                                    if (PointsMap[it_ij->second.first].Zlist[it_ij->second.second].is_local){
                                        if (PointsMap[it_ij->second.first].Zlist[it_ij->second.second].isZset){
                                            sum_z += PointsMap[it_ij->second.first].Zlist[it_ij->second.second].z;
                                        }
                                        else{
                                            //std::cout << "Proc " << my_rank << " has " << itz->dof << " with not set local " << itz->cnstr_nds[ii] << std::endl;
                                            all_known = false;
                                            break;
                                        }
                                    }
                                    else{ // exists in the dof_ij map but is not local
                                        not_local = true;
                                    }
                                }
                                else{ // doesn't even exists in the dof_ij map
                                    not_local = true;
                                }
                                if (not_local){
                                    std::map<int, double>::iterator it_elev;
                                    it_elev = elev_asked.find(itz->cnstr_nds[ii]);
                                    if (it_elev != elev_asked.end()){
                                        sum_z += it_elev->second;
                                    }
                                    else{
                                        //std::cout << "Proc " << my_rank << " has " << itz->dof << " with not set NONlocal " << itz->cnstr_nds[ii] << std::endl;
                                        all_known = false;
                                        dof_ask_map.insert(std::pair<int,int>(itz->cnstr_nds[ii],itz->cnstr_nds[ii]));
                                        break;
                                    }
                                }
                            }

                            if (all_known){
                                itz->z = sum_z / static_cast<double>(itz->cnstr_nds.size());
                                itz->isZset = true;
                            }
                            else{
                                count_not_set++;
                            }
                        }//-----------------------IS HANGING-------------------------------
                        else{
                            if (!itz->Top.isSet) {
                                // Check if the top is local
                                if (itz->Top.proc == static_cast<int>(my_rank)){
                                    it_ij = dof_ij.find(itz->Top.dof);
                                    if (it_ij != dof_ij.end()){
                                        if (PointsMap[it_ij->second.first].Zlist[it_ij->second.second].isZset){
                                            itz->Top.z = PointsMap[it_ij->second.first].Zlist[it_ij->second.second].z;
                                            itz->Top.isSet = true;
                                        }
                                        else{
                                            //std::cerr << "dof " << PointsMap[it_ij->second.first].Zlist[it_ij->second.second].dof << " has no Z for proc " << my_rank << std::endl;
                                        }
                                    }
                                    else{
                                        std::cerr << "Node with id " << itz->Top.dof << " is local for proc " << my_rank << " but was not found" << std::endl;
                                    }
                                }
                                else{
                                    // check if we already know its elevation from another processor
                                    std::map<int, double>::iterator it_elev;
                                    it_elev = elev_asked.find(itz->Top.dof);
                                    if (it_elev != elev_asked.end()){
                                        itz->Top.z = it_elev->second;
                                        itz->Top.isSet = true;
                                    }
                                    else{
                                        dof_ask_map.insert(std::pair<int,int>(itz->Top.dof,itz->Top.dof));
                                    }
                                }
                            }

                            if (!itz->Bot.isSet){
                                // Check if the bottom is local
                                if (itz->Bot.proc == static_cast<int>(my_rank)){
                                    it_ij = dof_ij.find(itz->Bot.dof);
                                    if (it_ij != dof_ij.end()){
                                        if (PointsMap[it_ij->second.first].Zlist[it_ij->second.second].isZset){
                                            itz->Bot.z = PointsMap[it_ij->second.first].Zlist[it_ij->second.second].z;
                                            itz->Bot.isSet = true;
                                        }
                                        else{
                                            //std::cerr << "dof " << PointsMap[it_ij->second.first].Zlist[it_ij->second.second].dof << " has no Z for proc " << my_rank << std::endl;
                                        }
                                    }
                                    else{
                                        std::cerr << "Node with id " << itz->Bot.dof << " is local for proc " << my_rank << " but was not found" << std::endl;
                                    }
                                }
                                else{
                                    // check if we already know its elevation from another processor
                                    std::map<int, double>::iterator it_elev;
                                    it_elev = elev_asked.find(itz->Bot.dof);
                                    if (it_elev != elev_asked.end()){
                                        itz->Bot.z = it_elev->second;
                                        itz->Bot.isSet = true;
                                    }
                                    else{
                                        dof_ask_map.insert(std::pair<int,int>(itz->Bot.dof,itz->Bot.dof));
                                    }
                                }
                            }

                            if (itz->Top.isSet && itz->Bot.isSet){
                                itz->z = itz->Top.z * itz->rel_pos + (1.0 - itz->rel_pos) * itz->Bot.z;
                                itz->isZset = true;
                            }
                            else{
                                count_not_set++;
                            }
                        }
                    }
                }
            }
        }

        MPI_Barrier(mpi_communicator);
        std::cout << "Proc " << my_rank << " has " << count_not_set << " not set and " << dof_ask_map.size() << " dofs asked so far" << std::endl;

        // Check if all points have been set
        std::vector<int> points_not_set(n_proc);
        Send_receive_size(static_cast<unsigned int>(count_not_set), n_proc, points_not_set, mpi_communicator);
        count_not_set = 0;
        for (unsigned int i = 0; i < n_proc; ++i)
            count_not_set = count_not_set + points_not_set[i];
        if (count_not_set == 0)
            break;

        if (dbg_cnt == 9)
            return;
        //std::cout << "Rank " << my_rank <<" : " << count_not_set << std::endl;

        //copy unknown dofs from map to vector
        for (unsigned int iproc = 0; iproc < n_proc; ++iproc)
            dof_ask[iproc].clear();
        for (std::map<int,int>::iterator itemp = dof_ask_map.begin(); itemp != dof_ask_map.end(); ++itemp)
            dof_ask[my_rank].push_back(itemp->first);

        MPI_Barrier(mpi_communicator);
        //std::cout << "Rank " << my_rank << " has " << dof_ask[my_rank].size() << std::endl;


        // if there are points that have unkonwn elevations from the local processor
        // communicate them with the other processors
        std::vector<int> dof_ask_size(n_proc);
        Send_receive_size(static_cast<unsigned int>(dof_ask[my_rank].size()), n_proc, dof_ask_size, mpi_communicator);
        Sent_receive_data<int>(dof_ask, dof_ask_size, my_rank, mpi_communicator, MPI_INT);

        // loop through the requested points and if there are dofs that are local with its elevation set
        // send them
        std::vector<std::vector<int>> dof_ask_reply(n_proc);
        std::vector<std::vector<double>> dof_ask_z(n_proc);
        for (unsigned int i_proc = 0; i_proc < n_proc; ++i_proc){
            if (i_proc == my_rank)
                continue;
            for (unsigned int i = 0; i < dof_ask[i_proc].size(); ++i){
                it_ij = dof_ij.find(dof_ask[i_proc][i]);
                if (it_ij != dof_ij.end()){
                    int ipnt = it_ij->second.first;
                    int iz = it_ij->second.second;
                    if (PointsMap[ipnt].Zlist[iz].is_local){
                        if (PointsMap[ipnt].Zlist[iz].isZset){
                            //std::cout << "I'm rank " << my_rank << " and I know the Z for " << dof_ask[i_proc][i] << " : " << PointsMap[ipnt].Zlist[iz].z << std::endl;
                            dof_ask_reply[my_rank].push_back(dof_ask[i_proc][i]);
                            dof_ask_z[my_rank].push_back(PointsMap[ipnt].Zlist[iz].z);
                        }
                    }
                }
            }
        }

        std::vector<int> reply_size(n_proc);
        Send_receive_size(static_cast<unsigned int>(dof_ask_reply[my_rank].size()), n_proc, reply_size, mpi_communicator);
        Sent_receive_data<int>(dof_ask_reply, reply_size, my_rank, mpi_communicator, MPI_INT);
        Sent_receive_data<double>(dof_ask_z, reply_size, my_rank, mpi_communicator, MPI_DOUBLE);
        // loop again to collect the new points that have Z.
        // Each processor collects all of them even has not asked about them
        for (unsigned int i_proc = 0; i_proc < n_proc; ++i_proc){
            if (i_proc == my_rank)
                continue;
            for (unsigned int i = 0; i < dof_ask_reply[i_proc].size(); ++i){
                elev_asked[dof_ask_reply[i_proc][i]] = dof_ask_z[i_proc][i];
            }
        }
        dbg_cnt++;
    }

    //std::cout << " Exit while loop" << std::endl;
    dbg_meshStructInfo3D("Test03_" + prefix + "_", my_rank);




    MPI_Barrier(mpi_communicator);
    std::cout << "Rank " << my_rank << " has converged" << std::endl;

    MPI_Barrier(mpi_communicator);

    // After we have finished with all updates in the z structure we have to copy the---------------------------------------
    // new values to the distributed vector
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        std::vector<Zinfo>::iterator itz = it->second.Zlist.begin();
        for (; itz != it->second.Zlist.end(); ++itz){
            if (distributed_mesh_vertices.in_local_range(static_cast<unsigned int >(itz->dof))){
                double dz = itz->z - distributed_mesh_vertices[static_cast<unsigned int >(itz->dof)];
                distributed_mesh_Offset_vertices[static_cast<unsigned int >(itz->dof)] = dz;
                distributed_mesh_vertices[static_cast<unsigned int >(itz->dof)] += dz;
            }
        }
    }


    // The compress sends the data to the processors that owns the data
    distributed_mesh_Offset_vertices.compress(VectorOperation::insert);
    distributed_mesh_vertices.compress(VectorOperation::insert); // This was commented in the original dev code


    // updates the elevations and offsets to the constraint nodes --------------------------
    mesh_constraints.distribute(distributed_mesh_Offset_vertices);
    mesh_Offset_vertices = distributed_mesh_Offset_vertices;

    mesh_constraints.distribute(distributed_mesh_vertices);
    mesh_vertices = distributed_mesh_vertices;

    //dbg_meshStructInfo3D("After3D_Elev_" + prefix + "_", my_rank);



    //move the actual vertices ------------------------------------------------
    move_vertices(mesh_dof_handler,
                  mesh_vertices,
                  my_rank, prefix);

    std::vector<bool> locally_owned_vertices = triangulation.get_used_vertices();
    typename parallel::distributed::Triangulation<dim>::active_cell_iterator
    cell = triangulation.begin_active(),
    endc = triangulation.end();
    for (; cell!=endc; ++cell){
        if (cell->is_artificial() ||
                (cell->is_ghost() && cell->subdomain_id() < triangulation.locally_owned_subdomain() )){
            for (unsigned int v=0; v<GeometryInfo<dim>::vertices_per_cell; ++v)
                locally_owned_vertices[cell->vertex_index(v)] = false;
        }
    }
    triangulation.communicate_locally_moved_vertices(locally_owned_vertices);

}

template <int dim>
void Mesh_struct<dim>::move_vertices(DoFHandler<dim>& mesh_dof_handler,
                                     TrilinosWrappers::MPI::Vector& mesh_vertices,
                                     unsigned int my_rank,
                                     std::string prefix){
    // for debuging just print the cell mesh
    const std::string mesh_file_name = ("mesh_after_" + prefix + "_" +
                                        Utilities::int_to_string(my_rank+1, 4) +
                                        ".dat");

    std::ofstream mesh_file;
    mesh_file.open((mesh_file_name.c_str()));

    typename DoFHandler<dim>::active_cell_iterator
    cell = mesh_dof_handler.begin_active(),
    endc = mesh_dof_handler.end();
    double x,y,z;
    for (; cell != endc; ++cell){
        if (cell->is_locally_owned()){//cell->is_artificial() == false
            for (unsigned int vertex_no = 0; vertex_no < GeometryInfo<dim>::vertices_per_cell; ++vertex_no){
                Point<dim> &v=cell->vertex(vertex_no);
                for (unsigned int dir=0; dir < dim; ++dir){
                    v(dir) = mesh_vertices(cell->vertex_dof_index(vertex_no, dir));
                    if (dir == 0)
                        x = v(dir)/dbg_scale_x;
                    if (dir == 1 && dim == 2){
                        y = v(dir)/dbg_scale_z;
                        z = 0;
                    }
                    if (dir == 1 && dim == 3){
                        z = v(dir)/dbg_scale_x;
                    }
                    if (dir == 2 && dim == 3)
                        y = v(dir)/dbg_scale_z;
                }
                mesh_file << x << ", " << y << ", " << z << ", ";
            }
            mesh_file << std::endl;
        }
    }
    mesh_file.close();
}

template<int dim>
void Mesh_struct<dim>::printMesh(std::string filename, unsigned int i_proc, DoFHandler<dim>& mesh_dof_handler){
    const std::string mesh_file_name = ("mesh_Print" + filename + "_" +
                                        Utilities::int_to_string(i_proc+1, 4) +
                                        ".dat");
    std::ofstream mesh_file;
    mesh_file.open((mesh_file_name.c_str()));

    typename DoFHandler<dim>::active_cell_iterator
    cell = mesh_dof_handler.begin_active(),
    endc = mesh_dof_handler.end();
    double x, y, z;
    for (; cell != endc; ++cell){
        if (cell->is_locally_owned()){
            for (unsigned int vertex_no = 0; vertex_no < GeometryInfo<dim>::vertices_per_cell; ++vertex_no){
                Point<dim> v=cell->vertex(vertex_no);
                for (unsigned int dir=0; dir < dim; ++dir){
                    if (dir == 0)
                        x = v(dir)/dbg_scale_x;
                    if (dir == 1 && dim == 2){
                        y = v(dir)/dbg_scale_z;
                        z = 0;
                    }
                    if (dir == 1 && dim == 3){
                         z = v(dir)/dbg_scale_x;
                    }
                    if (dir == 2 && dim == 3){
                        y = v(dir)/dbg_scale_z;
                    }
                }
                mesh_file << x << ", " << y << ", " << z << ", ";
            }
            mesh_file << std::endl;
        }
    }
    mesh_file.close();
}

template <int dim>
void Mesh_struct<dim>::dbg_set_scales(double xscale, double zscale){
    dbg_scale_x = xscale;
    dbg_scale_z = zscale;
}

template <int dim>
void Mesh_struct<dim>::set_id_above_below(int my_rank){
    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        it->second.set_ids_above_below(my_rank);
    }
}

template  <int dim>
void Mesh_struct<dim>::make_dof_ij_map(){
    dof_ij.clear();
    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        for (unsigned int k = 0; k < it->second.Zlist.size(); ++k){
            dof_ij[it->second.Zlist[k].dof] = std::pair<int,int> (it->first,k);
        }
    }
}

template <int dim>
void Mesh_struct<dim>::compute_initial_elevations(MyFunction<dim, dim-1> top_function,
                                                  MyFunction<dim, dim-1> bot_function,
                                                  std::vector<double>& vert_discr){
    std::vector<double>uniform_dist = linspace(0.0, 1.0, vert_discr.size());

    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        double top = top_function.value(it->second.PNT);
        double bot = bot_function.value(it->second.PNT);
        it->T = top;
        it->B = bot;
    }
}

template <int dim>
void Mesh_struct<dim>::identify_local_connections(){
    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        it->second.set_local_above_below();
    }
}

template <int dim>
void Mesh_struct<dim>::identify_dependencies(){
    typename std::map<int , PntsInfo<dim> >::iterator it;
    for (it = PointsMap.begin(); it != PointsMap.end(); ++it){
        std::vector<Zinfo>::iterator itz = it->second.Zlist.begin();
        for (; itz != it->second.Zlist.end(); ++itz){
            //it->second.
        }
    }
}


#endif // MESH_STRUCT_H
