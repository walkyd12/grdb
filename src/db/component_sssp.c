#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "graph.h"
#include "schema.h"
#include "tuple.h"
#include "cli.h"

/* Defines 999,999 as the max integer. Used as place holder in arrays */
#define INFINITY                9999
/* Size of attribute name. Used to store attribute name in an array */
#define ATTR_NAME_MAX_SIZE      sizeof(c->se->attrlist->name)

/* 
 * Start of helper function definitions. These functions apply concepts
 * found in other funcitons throughout, but are applied here to the 
 * sssp algorithm. Used to eliminate confusion and organize the main
 * component_sssp function.
 */

int min_weight(int w1, int w2)
{
	if(w1<w2) return w1;
	else return w2;
}


vertexid_t *list_compare(vertexid_t *vertices1, vertexid_t *vertices2, int number_of_verticies)
{
  	vertexid_t *temp_list = malloc(number_of_verticies * sizeof(vertexid_t));

  	for(int i = 0; i < number_of_verticies; i++){
  		temp_list[i] = INFINITY;
  	}

  	int same_counter = 0;
  	for(int i = 0;i < number_of_verticies; i++) {
  	 	int isEqual = 0;
    	for(int j = 0; j < number_of_verticies; j++) {
      
    		if(vertices1[i] == vertices2[j]) {
        		isEqual = 1;
        		break;
      		}
    	}
    	if(!isEqual) {
      		temp_list[i] = vertices1[i];
      		same_counter++;
    	}
  	}

  return temp_list;
}

/* Funciton that finds a vertexid_t in a list and returns the index */
int find_index(vertexid_t id, vertexid_t* list, int ctr){
    for(int i = 0; i < ctr; i++)
    {
      if(list[i] == id)
          return i;
    }
    return -1;
}

/* Helper function that finds the min value in a list */
vertexid_t get_min_from_list(vertexid_t *temp_list, int number_of_verticies)
{
  	vertexid_t min = INFINITY;
  	int index = -1;
  	for(int i = 0;i<number_of_verticies;i++){
    	if(temp_list[i] <= min) {
      		min = temp_list[i];
      		index = i;
    	}
  	}
  	assert(index != -1);
	return index;
}

/*
 * Helper function that returns the weight found on an edge between two vertices.
 * If there is no edge return INFINITY
 */
int get_edge_weight(component_t c, vertexid_t v1, vertexid_t v2, char attr_name[]){
 	struct edge e;
 	edge_t e1;
 	int offset, weight;

 	edge_init(&e);
 	edge_set_vertices(&e, v1, v2);
 	e1 = component_find_edge_by_ids(c, &e);

 	if ((e1 == NULL) || (v1 == v2))
 		return INFINITY;

 	offset = tuple_get_offset(e1->tuple, attr_name);
 	weight = tuple_get_int(e1->tuple->buf + offset);

 	return weight;
}

/* 
 * Helper function that takes in a component and pointer to an allocated
 * vertexid_t array. The function searches the vfd file to fill out the
 * pre-allocated array.
 */
void get_vertex_list(component_t c, vertexid_t *vertex_list)
{
	int count = 0;
  	off_t off;
	ssize_t len, size;
	vertexid_t id;
	char *buf;
	int readlen;

  	if (c->sv == NULL)
		size = 0;
	else
		size = schema_size(c->sv);

	readlen = sizeof(vertexid_t) + size;
	buf = malloc(readlen);

	for (off = 0;; off += readlen) {
		lseek(c->vfd, off, SEEK_SET);
		len = read(c->vfd, buf, readlen);
		if (len <= 0)
			break;

		id = *((vertexid_t *) buf);
    	vertex_list[count] = id;
    	count++;
  	}

  	free(buf);
}

/* 
 * Helper function to find the number of vertices in the component.
 * Used to calculate space needed to store vertex data.
 */
int vertex_count(component_t c)
{
  	int count = 0;

  	off_t off;
	ssize_t len, size;
	vertexid_t id;
	char *buf;
	int readlen;

  	if (c->sv == NULL)
		size = 0;
	else
		size = schema_size(c->sv);

	readlen = sizeof(vertexid_t) + size;
	buf = malloc(readlen);

	for (off = 0;; off += readlen) {
		lseek(c->vfd, off, SEEK_SET);
		len = read(c->vfd, buf, readlen);
		if (len <= 0)
			break;

		id = *((vertexid_t *) buf);
    	
    	count++;
  	}
	
	return count;
}

/* Helper function to find weight attribute (first INTEGER data type) */
int get_weight_attribute(component_t c, char *attr_name)
{
	struct attribute *tmp = c->se->attrlist;

  	while(tmp != NULL){

    	if(tmp->bt == INTEGER) {
#if _DEBUG
        	printf("Weight attribute found: [%s]\n", tmp->name);
#endif
      		memcpy(attr_name, tmp->name, ATTR_NAME_MAX_SIZE);

      		return 0;
    	} else {
     		tmp = tmp->next;
    	}
	}

	return -1;
}


/* 
 * Main function to compute shortest path between two given vertices in graph.
 * Utilizes helper functions found above in order to set up and execute
 * Dijkstra's algorithm on edge weights.
 */

int
component_sssp(
    	component_t c,
        vertexid_t v1,
        vertexid_t v2,
        int *n,
        int *total_weight,
        vertexid_t **path)
{
	if(v1 == v2) {
		printf("Start and end vertex cannot be the same!\n");
		return -1;
	}

	/* Set file descriptors */
	c->efd = edge_file_init(gno, cno);
	c->vfd = vertex_file_init(gno,cno);
	
	/*
	 * Figure out which attribute in the component edges schema you will
	 * use for your weight function
	 */

	/* 
	 * Use helper function get_weight_attribute to find name of
	 * first attribute with INTEGER base_type. 
	 * This will be considered weight attribute in all cases.
	 */
	char attr_name[ATTR_NAME_MAX_SIZE];
	
	if(get_weight_attribute(c, attr_name) < 0)
		return -1;

	/* Count the number of vertices in component with helper funciton */
  	int number_of_verticies = vertex_count(c);

#if _DEBUG
	printf("Number of Vertices: %d\n", number_of_verticies);
#endif

	/* 
	 * Use helper function get_vertex_list that takes in the component
	 * and a pre-allocated pointer to where to vertex_list will be stored.
	 * This pointer will be filled in as an array within the function with
	 * a list of all of the vertices.
	 */
  	vertexid_t *vertex_list    = malloc(number_of_verticies * sizeof(vertexid_t));
  	get_vertex_list(c, vertex_list);

  	vertexid_t *building_array = malloc(number_of_verticies * sizeof(vertexid_t));
  	int *current_shortest_path_cost = malloc(number_of_verticies * sizeof(vertexid_t));
  	vertexid_t *V_minus_S;

  	/* 
  	 * Initialize current_shortest_path_cost array with INFINITY to show
  	 * no edges have been traversed.
  	 */

  	int start_index;
  	int end_index;
  	for(int i = 0; i < number_of_verticies; i++){
   		if(vertex_list[i] == v1)
        	start_index = i;
      	if(vertex_list[i] == v2)
       		end_index = i;
       	current_shortest_path_cost[i] = INFINITY;
  	}

  	for(int i = 0; i < number_of_verticies; i++){
    	  int temp_weight = get_edge_weight(c, v1, vertex_list[i], attr_name);
    	  if(temp_weight != INFINITY){
    	      building_array[i] = v1;
    	  }
  	}

	vertexid_t v = 0;
	vertexid_t w = 0;
	int S_ctr = 0;

	for(int i = 1; i < number_of_verticies; i++){
	    current_shortest_path_cost[i] = get_edge_weight(c, v1, vertex_list[i], attr_name);

	    vertexid_t* S = malloc(number_of_verticies * sizeof(vertexid_t));
	    
	    for(int i = 0; i < number_of_verticies; i++){
	    	S[i] = 0;
	    }

	    S[0] = v1;
	    S_ctr = 1;


	    for(int j = 0; j < number_of_verticies; j++){
	    	V_minus_S = list_compare(vertex_list, S, number_of_verticies);
	    	w = get_min_from_list(V_minus_S, number_of_verticies);
	    	S[S_ctr] = vertex_list[w];
	    	S_ctr++;

	    	free(V_minus_S);


	    	for(int k = 0; k < number_of_verticies; k++)
	    	{
	    		V_minus_S = list_compare(vertex_list, S, number_of_verticies);
	    	    v = k;
	    	    if(V_minus_S[v] >= INFINITY) {
	    	    	free(V_minus_S);
	    	     	continue;
	    	    }

	    		if((current_shortest_path_cost[w] + get_edge_weight(c,vertex_list[w],vertex_list[v], attr_name) ) < current_shortest_path_cost[v])
	    		{
	        		building_array[v] = vertex_list[w];
	      		}
	      		current_shortest_path_cost[v] = min_weight(current_shortest_path_cost[v], (current_shortest_path_cost[w] + get_edge_weight(c,vertex_list[w],vertex_list[v], attr_name) ));
	    	}
	  	}	
	}


	int shortest_path_count = 0;
	vertexid_t *shortest_path = malloc(number_of_verticies * sizeof(vertexid_t));
	shortest_path[0] = v2;
	shortest_path_count = 1;
	vertexid_t tmp_builder = building_array[end_index];

	while(tmp_builder != v1)
	{
		shortest_path[shortest_path_count] = tmp_builder;
	    shortest_path_count++;
	    int found_index = find_index(tmp_builder, vertex_list, number_of_verticies);
	    tmp_builder = building_array[found_index];
	}
	shortest_path[shortest_path_count] = v1;
	shortest_path_count++;

	path = &shortest_path;

	printf("Total Weight: %d\n", current_shortest_path_cost[end_index]);
	printf("Shortest Path: [");
	for(int i = shortest_path_count-1; i >= 0; i--){
		if(i == 0) {
			printf("%llu]\n", shortest_path[i]);
		}else{
			printf("%llu,", shortest_path[i]);
		}
	}
	total_weight = &current_shortest_path_cost[end_index];
	n = &shortest_path_count;

	return (-1);
}