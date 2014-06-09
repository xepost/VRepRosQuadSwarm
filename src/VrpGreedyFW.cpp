//	Copyright (c) 2014, Francesco Wanderlingh. 					//
//	All rights reserved.										//
//	License: BSD (http://opensource.org/licenses/BSD-3-Clause)	//

/*
 * VrpGreedy.cpp
 *
 *  Created on: Apr 29, 2014
 *      Author: francescow
 */

#include "VrpGreedyFW.h"
#include "quadcopter_ctrl/FloydWarshall.h"
#include "Utils.h"
#include <iostream>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <ctime>


#define STARTNODE 5
#define MAX_FOV 2
#define SQRT2 1.4143
/// Ceiled to avoid failure in check condition in "solve()"
/// due to small decimal errors.

#define DEF_GRID_X 4
#define DEF_GRID_Y 4
#define DEF_NUM_ROB 3

using std::cout;
using std::endl;
using std::vector;


VrpGreedy::VrpGreedy() : gridSizeX(DEF_GRID_X),
    gridSizeY(DEF_GRID_Y),
    numAgents(DEF_NUM_ROB),  // Default Initialisation
    minDist(FLT_MAX),
    v(0)
{
  cout << "Default configuration:" << endl;
  cout << "- Default grid is empty" << endl;
  cout << "- " << numAgents << " Robots" << endl;

  access_vec.resize(gridSizeX*gridSizeY, 0);

  // Parameters initialisation
  deltaBest = FLT_MAX;
  deltavip = FLT_MAX;
  bigL = -FLT_MAX;
  bigLTent = -FLT_MAX;
  liMin = FLT_MAX;

}


VrpGreedy::VrpGreedy(std::string acc_matrix_path) : numAgents(DEF_NUM_ROB)
{
  loadMatrixFile(acc_matrix_path);
}


VrpGreedy::VrpGreedy(std::string acc_matrix_path, int agents)
{
  loadMatrixFile(acc_matrix_path);
  numAgents = agents;
}


VrpGreedy::~VrpGreedy()
{
  // TODO Auto-generated destructor stub
}


float VrpGreedy::pathLength(vector<graphNode> graph, vector<int> &path){


  float length = FLT_MIN;
  for(vector<graphNode>::size_type i = 0; i < (path.size() - 1); i++){
    length = length + sqrt(pow((graph[path[i]].posx - graph[path[i+1]].posx),2) +
                           pow((graph[path[i]].posy - graph[path[i+1]].posy),2) +
                           pow((graph[path[i]].posz - graph[path[i+1]].posz),2) );
  }
  return length;
}


void VrpGreedy::loadMatrixFile(std::string acc_matrix_path){
  /**
   * Here gridSizeX and gridSizeY are deducted from the size of the input matrix file
   */

  std::ifstream access_mat;
  access_mat.open( acc_matrix_path.c_str() );

  if( access_mat.is_open() ) {
    int val;
    int num_nl = 0;
    while( access_mat >> val ){
      if(access_mat.peek() == '\n') num_nl++;
      access_vec.push_back( val );
    }

    gridSizeX = num_nl;
    gridSizeY = access_vec.size()/num_nl;

    access_mat.close();

  }
  else{ cout << "Error reading file!" << endl; }

}


void VrpGreedy::createGraph(){

    const int n = gridSizeX*gridSizeY;

    assert(n == access_vec.size());
    vector<int> _graph(n, Inf);
    vector< vector<int> > __graph(n, _graph);
    graph = __graph;
    int row, col;    // Main indexes
    int row_shift, col_shift; // To move around spatial adjacents
    int nb_row, nb_col;       // Adjacent indexes

    printf("%sOccupancy Map:%s",TC_RED, TC_NONE);
    for(int j=0; j<n; j++){
      if(j%gridSizeY == 0) cout << endl;
      if( access_vec[j] == 1 ){
        printf("%s",TC_RED);
        Utils::spaced_cout(j);
        printf("%s", TC_NONE);
      }
      else Utils::spaced_cout(j);
    }
    cout << endl << endl;


    for(int i=0; i<n; i++){
      if(access_vec[i] == 0){
        row = i/gridSizeY;
        col = i%gridSizeY;
        for(row_shift=-1; row_shift<=1; row_shift++){
          for(col_shift=-1; col_shift<=1; col_shift++){
            nb_row = row + row_shift;
            nb_col = col + col_shift;
            if( (nb_row>=0) && (nb_row<gridSizeX) && (nb_col>=0) && (nb_col<gridSizeY) ///RANGE CHECK
              && (row_shift!=0 || col_shift!=0)  ///<--- don't check same node of current
              && (row_shift*col_shift == 0)  ///<--- don't allow diagonal movements
              )
              //&& ( (row_shift*row_shift xor col_shift*col_shift)==1 ) <--last 2 statements compressed in one condition
            {
              if(access_vec[nb_row*gridSizeY+nb_col] == 0){
                /// Create the edge between "i" and its "free neighbour"
                graph[i][nb_row*gridSizeY+nb_col] = 1;
                graph[nb_row*gridSizeY+nb_col][i] = 1;
              }
            }
          }
        }
      }
    }

    distanceMat = graph;

    for(int i=0; i<n; i++) distanceMat[i][i] = 0;

}


void VrpGreedy::init(){

  createGraph();

  //cout << "Matrix size is: " << gridSizeX << "x" << gridSizeY << endl;

  graphNodes.resize(gridSizeX*gridSizeY);
  unvisitedNodes.reserve( graphNodes.size() );

  access_vec.at(STARTNODE) = 1;    //STARTNODE is set as start for all the agents


  /// Graph initialisation - to every node is assigned a position
  for(int i=0; i<gridSizeX; i++){
    for(int j=0; j<gridSizeY; j++){
      graphNodes.at((i*gridSizeY) + j).setPos((float)i*2, (float)j*2);
      graphNodes.at((i*gridSizeY) + j).occupied = access_vec.at((i*gridSizeY) + j);
      //cout << (int)graphNodes.at((i*gridSizeY) + j).occupied << " ";
      if (graphNodes.at((i*gridSizeY)+ j).occupied == 0){
        unvisitedNodes.push_back((i*gridSizeY) + j); //Adding the free nodes to the list of unvisited
      }
    }
    //cout << endl;
  }

  numFreeNodes = unvisitedNodes.size();

  minDist = (dist(graphNodes.at(0), graphNodes.at(1)) + FLT_MIN)*SQRT2;
  //cout << "minDist=" << minDist << endl;

  // Path initialisation
  path.push_back(STARTNODE);       // Every path initially is just 2 nodes: Start + End(=start)
  path.push_back(STARTNODE);

  for(int i = 0; i < numAgents; i++){
    Paths.push_back(path); // Define a path for every agent
  }

  Paths.reserve( graphNodes.size() * numAgents);

  createCycles();
}


void VrpGreedy::createCycles(){

  ///////////// Create initial cycles //////////////////////////////////////////

  /* To understand the need for the initial cycle: consider that the initial path
   * is (0,0), the next best path for the Greedy solver cannot be (0,1,0) which has
   * length=2, but (0,0,1) which has length=1. So it will not produce a circular
   * solution. By inserting the first element in the middle (see **), we solve this problem.
   */

  int insertIndex;


  for (itr = Paths.begin(); itr != Paths.end(); ++itr){            // On every path i

    // Delta increments initialisation
    deltaBest = FLT_MAX;
    deltavip = FLT_MAX;
    liMin = FLT_MAX;
    itc = (itr->begin()+1);        // Between Start and End
    for (v = 0; v < unvisitedNodes.size(); v++ ){   // For every Node v
      insertIndex = (int)(itc - itr->begin());        // This is necessary to avoid modifying the current path
      // vector (which is NOT allowed since we're iterating
      // inside it), and keep track of the insertion index.
      pathTentative = *itr;
      vector<int>::iterator iTent = pathTentative.begin() + insertIndex;
      float tentPathLength;
      pathTentative.insert(iTent, unvisitedNodes[v]);
      tentPathLength = pathLength(graphNodes, pathTentative);

      if(tentPathLength < liMin){
        liMin = tentPathLength;
        choice.set_vip(v, itr, itc);
      }

    } // END V
    choice.i->insert(choice.p, unvisitedNodes[choice.v]);
    vector<int>::iterator insertedNode = unvisitedNodes.begin() + choice.v;
    unvisitedNodes.erase(insertedNode);
  } // END I

}


void VrpGreedy::solve(){

  init();

  FloydWarshall myFW(graph);
  myFW.solve(distanceMat);

  //myFW.printMatrix(graph);

  //cout << "\nDistances: ";
  //myFW.printMatrix(distanceMat);

  int start = 4, target = 54;
  assert(start<gridSizeX*gridSizeY);
  assert(target<gridSizeX*gridSizeY);

  printf("%sThe path from %d to %d%s",  TC_CYAN, start, target, TC_NONE);
  myFW.getPath(start, target, fwPath);
  if( fwPath.size() > 0 ){
    printf("%s is long %d and goes through:%s\n",
           TC_CYAN, distanceMat[start][target], TC_NONE);
    //if( fwPath.size() == 2 ) cout << "Come on man, they're adjacent!\n";
    //else{
      for(int k=0; k<fwPath.size(); k++){
        cout << fwPath.at(k) << " ";
      }
      cout << endl;
    //}
  }

  fwPath.clear();
  start = 4; target = 175;

  printf("%sThe path from %d to %d%s",  TC_CYAN, start, target, TC_NONE);
    myFW.getPath(start, target, fwPath);
    if( fwPath.size() > 0 ){
      printf("%s is long %d and goes through:%s\n",
             TC_CYAN, distanceMat[start][target], TC_NONE);
      //if( fwPath.size() == 2 ) cout << "Come on man, they're adjacent!\n";
      //else{
        for(int k=0; k<fwPath.size(); k++){
          cout << fwPath.at(k) << " ";
        }
        cout << endl;
      //}
    }


  std::cin.get();

  int insertIndex;

  while(unvisitedNodes.size() > 0){
/*
    std::cout << "The contents of Paths are:" << endl;
    for (itr = Paths.begin(); itr != Paths.end(); ++itr){
      cout << "#" << itr - Paths.begin() << ": ";
      for (itc = itr->begin(); itc != itr->end(); ++itc){
        std::cout << *itc << ' ';
      }
      std::cout << '\n';
    }
*/
    // Delta increments initialisation
    deltaBest = FLT_MAX;
    deltavip = FLT_MAX;
    bigL = -FLT_MAX;
    bigLTent = -FLT_MAX;
    liMin = FLT_MAX;

    // Initialise bigL
    for(vector< vector<int> >::iterator it = Paths.begin(); it!=Paths.end(); ++it){
      if( pathLength(graphNodes, *it) > bigL ) bigL = pathLength(graphNodes, *it);
    }

    ///*** MAIN NESTED LOOPS ***///

    for (v = 0; v < unvisitedNodes.size(); v++ ){                      // For every Node v
      for (itr = Paths.begin(); itr != Paths.end(); ++itr){            // On every path i
        liMin = FLT_MAX;
        for (itc = (itr->begin()+1); itc != itr->end(); ++itc){        // In every position p (reversed)

          //cout << (dist(graphNodes[*itc], graphNodes[unvisitedNodes[v]])) << endl;
          //cout << (dist(graphNodes[*itc-1], graphNodes[unvisitedNodes[v]])) << endl;
          //std::cin.get();

          if( ( (dist(graphNodes[*itc], graphNodes[unvisitedNodes[v]])) <= minDist+MAX_FOV   &&
                (dist(graphNodes[*(itc - 1)], graphNodes[unvisitedNodes[v]])) <= minDist+MAX_FOV ) ){

            insertIndex = (int)(itc - itr->begin());        // This is necessary to avoid modifying the current path
            // vector (which is NOT allowed since we're iterating
            // inside it), and keep track of the insertion index.
            pathTentative = *itr;
            vector<int>::iterator iTent = pathTentative.begin() + insertIndex;
            float tentPathLenght;

            pathTentative.insert(iTent, unvisitedNodes[v]);
            tentPathLenght = pathLength(graphNodes, pathTentative);

            if(tentPathLenght < liMin){
              liMin = tentPathLenght;

              /// *** Objective Function *** ///
              deltavip = liMin - bigL;

              if(deltavip < deltaBest){
                deltaBest = deltavip;
                choice.set_vip(v, itr, itc);
              }//End Check Global Best
            }//End Check Local Best

          }///DISTANCE CHECK - work in progress
        } // END P(positions)
      } // END I (robots)
    } // END V (nodes)



    choice.i->insert(choice.p, unvisitedNodes[choice.v]);   // Inserting chosen best node

    vector<int>::iterator insertedNode = unvisitedNodes.begin() + choice.v;   //Retrieve the index of the just inserted node
    unvisitedNodes.erase(insertedNode);     // Delete it from list of unvisited

    cout << "unvisitedNodes.size() = " << (int)unvisitedNodes.size() << "       " << '\r';
    cout.flush();

  } // END WHILE

  std::cout << '\n';

  std::cout << "The contents of Paths are:" << endl;
  for (itr = Paths.begin(); itr != Paths.end(); ++itr){
    cout << "#" << itr - Paths.begin() << ": ";
    for (itc = itr->begin(); itc != itr->end(); ++itc){
      std::cout << *itc << ' ';
    }
    std::cout << '\n';
  }

}


void VrpGreedy::copyPathsTo(vector< vector<int> > &destination){
  destination = Paths;
}

void VrpGreedy::copyGraphTo(vector< graphNode > &destination){
  destination = graphNodes;
}

float VrpGreedy::dist(graphNode &a, graphNode &b){

  float dist = sqrt ( pow(a.posx - b.posx, 2.0) +
                      pow(a.posy - b.posy, 2.0) +
                      pow(a.posz - b.posz, 2.0) );

  //cout << "Dist=" << dist << endl;
  return dist;
}

