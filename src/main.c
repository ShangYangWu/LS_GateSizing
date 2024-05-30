#include <time.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <math.h>
#include "base/abc/abc.h"

#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define FIX_NEG_ZERO(x) (fabs(x) < 1e-10 ? 0.0 : (x))

// procedures to start and stop the ABC framework
// (should be called before and after the ABC procedures are called)
extern void  Abc_Start();
extern void  Abc_Stop();

extern void util_getopt_reset ARGS((void));
extern Abc_Ntk_t * Io_ReadBlifAsAig(char *, int);

// gcc -O3 main.o /home/shangyang/alanmi-abc-906cecc894b2//libabc.a -o ace -lm -ldl -lrt -rdynamic -lreadline -lhistory -ltermcap -lpthread

//***********************************************************
// structures
typedef struct lib_gate{
    char *name;
    enum {INV, NAND} gate_type;
    double timing[2]; // timing[0]: fixed timing, timing[1]: timing *= output
    double area;
} lib_gate;

typedef struct node{
	Abc_Obj_t *obj;
	enum {GATE, PI, PO} type;
	double delay;	// node output result time
	double arrival0; // inv0 arrival time
	double arrival1; // inv1 arrival time	
	double required_time;	// node required time
	double slack;	// nand slack
	double inv_slack0;	// inv0 slack
	double inv_slack1;	// inv1 slack
	int inv0_id; // fanin0 using inverter_id in library / -1: no inverter
	int inv1_id; // fanin1 using inverter_id in library / -1: no inverter
	int nand_id; // using NAND_id in library / -1: not NAND gate
	int inv0_gateID; // renaming
	int inv1_gateID; // renaming
	int nand_gateID; // renaming				
	int inputs[2]; // input node index in nodes[]
} node;

//***********************************************************
// static variables
unsigned int node_count = 0;
node nodes[2500]; // max bench mark: c6288.blif 2401 nodes
unsigned int inv_count = 0;
lib_gate inverters[8]; // library data count: 8
unsigned int nand_count = 0;
lib_gate nands[8]; // library data count: 8
double _initial_delay = 0;
double _original_area = 0;
double _optimized_area = 0;
unsigned int _inv = 0; // inverter count
unsigned int nand = 0; // nand gate count

//***********************************************************
// functions
void PrintEachObj(Abc_Ntk_t *ntk){
	Abc_Obj_t *node, *fi;
	int i, j;
	
	printf("<< Print Each Obj- >>\n");
	printf(" ID       Name  Type  Level\n");
	printf("--------------------\n");
	Abc_NtkForEachObj(ntk, node, i){
		printf("%3d %10s %2d %2d\n", node->Id, Abc_ObjName(node), node->Type, node->Level);

		Abc_ObjForEachFanin(node, fi, j){
			printf("  %10s %d\n", Abc_ObjName(fi), Abc_ObjFaninC(node, j));
		}
	}
	printf("<< ----- End ----- >>\n");
}

void parseLib(){
	char line[20];
	bool turn = 0; // 0: inv, 1: nand

	// open file
	FILE *file = fopen("PA3.lib", "r");
	if(!file){
		printf("Error: cannot open file\n");
		return;
	}

	// read file
	while(fgets(line, sizeof(line), file)){
		unsigned  len = strlen(line);
		if(line[len-1] == '\n') line[len-1] = '\0';

		char *token = strtok(line, " ");
		if(token == NULL) continue;

		// token = gate_name
		if(strstr(token, "INV") || strstr(token, "NAND")){
			lib_gate gate;
			gate.name = strdup(token);

			if (strstr(token, "INV")){
				turn = 0;
				gate.gate_type = INV;
				inverters[inv_count] = gate;
			}
			else{
				turn = 1;
				gate.gate_type = NAND;
				nands[nand_count] = gate;
			}
		}
		else{
			// token = timing
			if(strcmp(token, "Timing") == 0){
				token = strtok(NULL, " ");
				if(turn == 0){
					inverters[inv_count].timing[0] = atof(token);
					token = strtok(NULL, " ");
					inverters[inv_count].timing[1] = atof(token);
				}else{
					nands[nand_count].timing[0] = atof(token);
					token = strtok(NULL, " ");
					nands[nand_count].timing[1] = atof(token);		
				}
			}

			// token = area
			else{
				token = strtok(NULL, " ");
				if(turn == 0){
					inverters[inv_count].area = atof(token);
					inv_count++;
				}else{
					nands[nand_count].area = atof(token);	
					nand_count++;
				}				
			}
		}
		token = strtok(NULL, " ");
	}
}

void sortLib(){
	// sort libraries from small area to large area
	for(unsigned int i=0; i<inv_count; i++){
		for(unsigned int j=0; j<i; ++j){
			if(inverters[j].area > inverters[i].area){
				lib_gate temp = inverters[j];
				inverters[j] = inverters[i];
				inverters[i] =  temp;
			}
		}
	}

	for(unsigned int i=0; i<nand_count; i++){
		for(unsigned int j=0; j<i; ++j){
			if(nands[j].area > nands[i].area){
				lib_gate temp = nands[j];
				nands[j] = nands[i];
				nands[i] =  temp;
			}
		}
	}
}

void mapping(Abc_Ntk_t *ntk){
	// AND to NAND + INV
	Abc_Obj_t *obj, *fi;
	int i, j;
	// traverse through all objects that may have inputs
	Abc_NtkForEachObj(ntk, obj, i){
		if (obj->Type == ABC_OBJ_PI){
			continue;
		}
		Abc_ObjForEachFanin(obj, fi, j){
			if(fi->Type != ABC_OBJ_PI){
				// if input already has INV => add in an INV will make it become redundant INV
				// if input doesn't have INV => add in an INV will make it become an INV
				if (j == 0){
					obj->fCompl0 = !obj->fCompl0;
				}else{
					obj->fCompl1 = !obj->fCompl1;
				}
			}
		}
	}
}

int getIndex(Abc_Obj_t *obj){
	// for getting the node index in nodes
	for (int idx = 0; idx < node_count; idx++){
		if (nodes[idx].obj->Id == obj->Id)
			return idx;
	}
	return -1;
}

void initNode(Abc_Obj_t* obj){
	nodes[node_count].obj = obj;
	nodes[node_count].delay = 0.0 ;
	nodes[node_count].arrival0 = 0.0 ;
	nodes[node_count].arrival1 = 0.0 ;
	nodes[node_count].required_time = DBL_MAX;
	nodes[node_count].slack = 0.0 ;
	nodes[node_count].inv_slack0 = 0.0 ;
	nodes[node_count].inv_slack1 = 0.0 ;
	nodes[node_count].inv0_id = -1;
	nodes[node_count].inv1_id = -1;
	nodes[node_count].nand_id = -1;
	nodes[node_count].inv0_gateID = 0; // renaming purpose
	nodes[node_count].inv1_gateID = 0; // renaming purpose
	nodes[node_count].nand_gateID = 0; // renaming purpose

	if(obj->Type == ABC_OBJ_PI){
		// only PIs don't have to compare required_time with min()
		nodes[node_count].required_time = 0.0 ;

	}else{
		// getting the node index of inputs
		Abc_Obj_t* fanin;
		int i;
		Abc_ObjForEachFanin(obj, fanin, i) {
			int index = getIndex(fanin);
			nodes[node_count].inputs[i] = index;
		}
	}
}

void createnodes(Abc_Ntk_t *ntk){
	Abc_Obj_t *obj;
	int i;
	// Initialized in topological order
	Abc_NtkForEachPi(ntk, obj, i){
		nodes[node_count].type = PI;
		initNode(obj);
		++node_count;
	}
	Abc_NtkForEachNode(ntk, obj, i){
		nodes[node_count].type = GATE;
		initNode(obj);
		++node_count;
	}
	Abc_NtkForEachPo(ntk, obj, i){
		nodes[node_count].type = PO;
		initNode(obj);
		++node_count;
	}
}

void initialDelay(){
	// calculate delay for each gate
	for(unsigned int node_id=0; node_id < node_count; node_id++){
		if(nodes[node_id].type == PI){
			continue;
		}else{
			// PO + INV
			if(nodes[node_id].type == PO){
				// PO has only one fanin
				nodes[node_id].arrival0 = nodes[nodes[node_id].inputs[0]].delay;
				if(nodes[node_id].obj->fCompl0 == 1){
					// get the fastest inverter in library
					double min_delay = DBL_MAX;
					int selected_inv_id = -1;
					for(int inv=0; inv<inv_count; ++inv){
						double delay = inverters[inv].timing[0] + inverters[inv].timing[1];
						if(min_delay > delay){
							selected_inv_id = inv;
							min_delay = delay;
						}
					}
					nodes[node_id].delay += min_delay;
					nodes[node_id].inv0_id = selected_inv_id;
					if(selected_inv_id != -1){
						_original_area += inverters[selected_inv_id].area; // update original_area
						_inv++;
					}
				}	
				nodes[node_id].delay += nodes[nodes[node_id].inputs[0]].delay;
				_initial_delay = max(nodes[node_id].delay, _initial_delay); // update initial_delay with POs
			}else{
				// NAND + INV
				nodes[node_id].arrival0 = nodes[nodes[node_id].inputs[0]].delay;
				nodes[node_id].arrival1 = nodes[nodes[node_id].inputs[1]].delay;
				// NAND has two fanins
				// double fanin0_delay=max(nodes[node_id].arrival0, nodes[node_id].arrival1),
				// 	   fanin1_delay=max(nodes[node_id].arrival0, nodes[node_id].arrival1);
				double fanin0_delay = nodes[node_id].arrival0,
					   fanin1_delay = nodes[node_id].arrival1;
				// fanin0
				if(nodes[node_id].obj->fCompl0 == 1){
					// get the fastest inverter in library
					double min_delay = DBL_MAX;
					int selected_inv_id = -1;
					for(int inv=0; inv<inv_count; inv++){
						double delay = inverters[inv].timing[0] + inverters[inv].timing[1];
						if(min_delay > delay){
							selected_inv_id = inv;
							min_delay = delay;
						}
					}
					fanin0_delay += min_delay;
					nodes[node_id].inv0_id = selected_inv_id; // inverter library id
					if(selected_inv_id != -1){
						_original_area += inverters[selected_inv_id].area; // update original_area
						_inv++;		
					}
				}
				// fanin1
				if(nodes[node_id].obj->fCompl1 == 1){
					// get the fastest inverter in library
					double min_delay = DBL_MAX;
					int selected_inv_id = -1;
					for(int inv=0; inv<inv_count; inv++){
						double delay = inverters[inv].timing[0] + inverters[inv].timing[1];
						if(min_delay > delay){
							selected_inv_id = inv;
							min_delay = delay;
						}
					}
					fanin1_delay += min_delay;
					nodes[node_id].inv1_id = selected_inv_id; // inverter library id
					if(selected_inv_id != -1){
						_original_area += inverters[selected_inv_id].area; // update original_area
						_inv++;
					}
				}
				nodes[node_id].delay += max(fanin0_delay, fanin1_delay);
				
				// nand
				// get the fastest nand in library
				double min_delay = DBL_MAX;
				int selected_nand_id = -1;
				for(int nand_id=0; nand_id<nand_count; nand_id++){
					double delay = nands[nand_id].timing[0] + nands[nand_id].timing[1]*Abc_ObjFanoutNum(nodes[node_id].obj);
					if(min_delay > delay){
						selected_nand_id = nand_id;
						min_delay = delay;
					}
				}
				nodes[node_id].nand_id = selected_nand_id; // nand library id
				nodes[node_id].delay += min_delay;
				if(selected_nand_id != -1){
					_original_area += nands[selected_nand_id].area; // update original_area
					nand++;	
				}
			}
		}
	}

	// calculate required_time for each gate
	for(int node_id = node_count-1; node_id >=0; node_id--){
		if(nodes[node_id].type == PI){
			continue;
		}else{
			if(nodes[node_id].type == PO){
				nodes[node_id].required_time = min(_initial_delay, nodes[node_id].required_time); // critical path time
				if(nodes[nodes[node_id].inputs[0]].type!=PI){ // update only PO and gates required_time
					if(nodes[node_id].obj->fCompl0 == 1){
						nodes[nodes[node_id].inputs[0]].required_time = min(nodes[node_id].required_time - (inverters[nodes[node_id].inv0_id].timing[0] + inverters[nodes[node_id].inv0_id].timing[1]), nodes[nodes[node_id].inputs[0]].required_time);
					}else{
						nodes[nodes[node_id].inputs[0]].required_time = min(nodes[node_id].required_time, nodes[nodes[node_id].inputs[0]].required_time);
					}
				}
			}
			else{
				double nand_delay = nands[nodes[node_id].nand_id].timing[0] + nands[nodes[node_id].nand_id].timing[1]*Abc_ObjFanoutNum(nodes[node_id].obj);
				if(nodes[nodes[node_id].inputs[0]].type!=PI){ // update only PO and gates required_time
					if(nodes[node_id].obj->fCompl0 == 1){
						nodes[nodes[node_id].inputs[0]].required_time = min(nodes[node_id].required_time - (nand_delay + inverters[nodes[node_id].inv0_id].timing[0] + inverters[nodes[node_id].inv0_id].timing[1]), nodes[nodes[node_id].inputs[0]].required_time);
					}else{
						nodes[nodes[node_id].inputs[0]].required_time = min(nodes[node_id].required_time - nand_delay, nodes[nodes[node_id].inputs[0]].required_time);
					}
				}
				if(nodes[nodes[node_id].inputs[1]].type!=PI){ // update only PO and gates required_time
					if(nodes[node_id].obj->fCompl1 == 1){
						nodes[nodes[node_id].inputs[1]].required_time = min(nodes[node_id].required_time - (nand_delay + inverters[nodes[node_id].inv1_id].timing[0] + inverters[nodes[node_id].inv1_id].timing[1]), nodes[nodes[node_id].inputs[1]].required_time);
					}else{
						nodes[nodes[node_id].inputs[1]].required_time = min(nodes[node_id].required_time - nand_delay, nodes[nodes[node_id].inputs[1]].required_time);
					}
				}
			}
		}
	}
}

void optimization(){
	// greedy algo: find the smallest gate in library to replace the orignal gate
	// step1: update NAND
	// step2: update INV1 and INV2
	for (int i = 0; i < node_count; i++){
		if(nodes[i].type == PI){
			nodes[i].delay = 0; // ensure PIs delay isn't modified
		}else{
			if(nodes[i].type == PO){
				nodes[i].arrival0 = nodes[nodes[i].inputs[0]].delay; // get arrival time
				if(nodes[i].obj->fCompl0 == 1){
					nodes[i].inv_slack0 = nodes[i].required_time - nodes[i].arrival0; // only INV has slack
					if(nodes[i].inv_slack0 > 0){
						for (int j = 0; j < inv_count; j++){ // find the smallest INV to replace the original INV
							double new_time = inverters[j].timing[0] + inverters[j].timing[1];
							if (new_time <= nodes[i].inv_slack0){
								nodes[i].inv_slack0 -= new_time;
								nodes[i].inv0_id = j;
								nodes[i].delay = nodes[i].arrival0 + new_time; // update delay for node
								_optimized_area += inverters[nodes[i].inv0_id].area;
								break;
							}
						}
					}
				}else{
					nodes[i].delay = nodes[i].arrival0; // update delay for node
				}
			}
			else{
				// get fanin0 arrival time
				nodes[i].arrival0 = nodes[nodes[i].inputs[0]].delay;
				double inv_time1 = 0.0, nand_arrival1 = nodes[i].arrival0;
				if(nodes[i].obj->fCompl0 == 1){
					inv_time1 = inverters[nodes[i].inv0_id].timing[0] + inverters[nodes[i].inv0_id].timing[1];
					nand_arrival1 += inv_time1;
				}

				// get fanin1 arrival time
				nodes[i].arrival1 = nodes[nodes[i].inputs[1]].delay;  
				double inv_time2 = 0.0, nand_arrival2 = nodes[i].arrival1;
				if(nodes[i].obj->fCompl1 == 1){
					inv_time2 = inverters[nodes[i].inv1_id].timing[0] + inverters[nodes[i].inv1_id].timing[1];
					nand_arrival2 += inv_time2;
				}

				// step1: find better NAND
				double nand_slack = nodes[i].required_time - max(nand_arrival1, nand_arrival2);
				double inv1_slack = nodes[i].required_time - nodes[i].arrival0;
				double inv2_slack = nodes[i].required_time - nodes[i].arrival1;
				double nand_time = nands[nodes[i].nand_id].timing[0] + (nands[nodes[i].nand_id].timing[1]  * Abc_ObjFanoutNum(nodes[i].obj));

				if (nand_slack > 0){
					for (int j = 0; j< nand_count; j++){ // find the smallest NAND to replace the original NAND
						double new_time = nands[j].timing[0] + (nands[j].timing[1]  * Abc_ObjFanoutNum(nodes[i].obj));
						if (new_time <= nand_slack){
							nand_time = new_time;
							nodes[i].nand_id = j;
							break;
						}
					}
				}
				nand_slack -= nand_time;
				inv1_slack -= nand_time;
				inv2_slack -= nand_time;
				_optimized_area += nands[nodes[i].nand_id].area; // update optimized_area
				nodes[i].slack = nand_slack;

				// step2: find better INV0 (independent to INV1)
				if(nodes[i].obj->fCompl0 == 1){
					if (inv1_slack > 0){
						for (int j = 0; j < inv_count; j++){ // find the smallest INV to replace the original INV
							double new_time = inverters[j].timing[0] + inverters[j].timing[1];
							if (new_time <= inv1_slack){
								inv1_slack -= new_time;
								inv_time1 = new_time;
								nand_arrival1 = nodes[i].arrival0 + new_time;
								nodes[i].inv0_id = j;
								break;
							}
						}
					}
					_optimized_area += inverters[nodes[i].inv0_id].area; // update optimized_area
					nodes[i].inv_slack0 = inv1_slack;
				}

				// step2: find better INV1 (independent to INV0)
				if(nodes[i].obj->fCompl1 == 1){
					if (inv2_slack > 0){
						for (int j = 0; j < inv_count; j++){ // find the smallest INV to replace the original INV
							double new_time = inverters[j].timing[0] + inverters[j].timing[1];
							if (new_time <= inv2_slack){
								inv2_slack -= new_time;
								inv_time2 = new_time;
								nand_arrival2 = nodes[i].arrival1 + new_time;
								nodes[i].inv1_id = j;
								break;
							}
						}
					}
					_optimized_area += inverters[nodes[i].inv1_id].area; // update optimized_area
					nodes[i].inv_slack1 = inv2_slack;
				}				
				nodes[i].delay = max(nand_arrival1, nand_arrival2) + nand_time; // update node delay
				if(nodes[i].delay > _initial_delay){
					printf("error");
				}
			}
		}
	}
}

int int_length(int num) {
    if (num == 0) return 1;
    int length = 0;
    if (num < 0) {
        length = 1;
        num = -num;
    }
    return length + (int)log10(num) + 1;
}

void Write(const char *pFileName, Abc_Ntk_t *ntk){
	FILE *pFile;
	pFile = fopen(pFileName, "w");
	if (pFile == NULL)
	{
		fprintf(stdout, "Io_WriteBench(): Cannot open the output file.\n");
		return;
	}

	fprintf(pFile, "Initial delay : %.3f\n", _initial_delay);
	fprintf(pFile, "Original area : %.3f\n", _original_area);
	fprintf(pFile, "Optimized area : %.3f\n", _optimized_area);

	Abc_Obj_t *obj;
	int i;
	Abc_NtkForEachPi(ntk, obj, i)
		fprintf(pFile, "INPUT(%s)\n", Abc_ObjName(obj));
	Abc_NtkForEachPo(ntk, obj, i)
		fprintf(pFile, "OUTPUT(%s)\n", Abc_ObjName(obj));

	int fixed_length = 45;
	for (int i = 0, g = 1; i < node_count; i++){
		if(nodes[i].type == PI){
			continue;
		}else{
			if(nodes[i].type == PO){
				if (nodes[nodes[i].inputs[0]].type != PI){
					if(nodes[i].obj->fCompl0){
						int padding = fixed_length - 7 - (int_length(g) + strlen(inverters[nodes[i].inv0_id].name) + int_length(nodes[nodes[i].inputs[0]].nand_gateID));
						fprintf(pFile, "X%d = %s(X%d)%*s slack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, nodes[nodes[i].inputs[0]].nand_gateID, padding, "", FIX_NEG_ZERO(nodes[i].inv_slack0));
						// fprintf(pFile, "X%d = %s(X%d)\t\t\tslack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, nodes[nodes[i].inputs[0]].nand_gateID, nodes[i].inv_slack0);
					}
				}
				// fanin0 is PI
				else{
					if(nodes[i].obj->fCompl0){
						int padding = fixed_length - 6 - (int_length(g) + strlen(inverters[nodes[i].inv0_id].name) + strlen(Abc_ObjName(nodes[nodes[i].inputs[0]].obj)));
						fprintf(pFile, "X%d = %s(%s)%*s slack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, Abc_ObjName(nodes[nodes[i].inputs[0]].obj), padding, "", FIX_NEG_ZERO(nodes[i].inv_slack0));						
						// fprintf(pFile, "X%d = %s(%s)\t\t\tslack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, Abc_ObjName(nodes[nodes[i].inputs[0]].obj), nodes[i].inv_slack0);
					}
				}
			}else{
				char input1[10];
				char input2[10];
				// fanin0
				if (nodes[nodes[i].inputs[0]].type != PI){
					if(nodes[i].obj->fCompl0){
						nodes[i].inv0_gateID = g;

						int padding = fixed_length - 7 - (int_length(g) + strlen(inverters[nodes[i].inv0_id].name) + int_length(nodes[nodes[i].inputs[0]].nand_gateID));
						fprintf(pFile, "X%d = %s(X%d)%*s slack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, nodes[nodes[i].inputs[0]].nand_gateID, padding, "", FIX_NEG_ZERO(nodes[i].inv_slack0));
						// fprintf(pFile, "X%d = %s(X%d)\t\t\tslack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, nodes[nodes[i].inputs[0]].nand_gateID, nodes[i].inv_slack0);
						sprintf(input1, "X%d", nodes[i].inv0_gateID);
					}else{
						sprintf(input1, "X%d", nodes[nodes[i].inputs[0]].nand_gateID);
					}
				}
				// fanin0 is PI
				else{
					if(nodes[i].obj->fCompl0){
						nodes[i].inv0_gateID = g;

						int padding = fixed_length - 6 - (int_length(g) + strlen(inverters[nodes[i].inv0_id].name) + strlen(Abc_ObjName(nodes[nodes[i].inputs[0]].obj)));
						fprintf(pFile, "X%d = %s(%s)%*s slack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, Abc_ObjName(nodes[nodes[i].inputs[0]].obj), padding, "", FIX_NEG_ZERO(nodes[i].inv_slack0));
						// fprintf(pFile, "X%d = %s(%s)\t\t\tslack : %.2f\n", g++, inverters[nodes[i].inv0_id].name, Abc_ObjName(nodes[nodes[i].inputs[0]].obj), nodes[i].inv_slack0);
						sprintf(input1, "X%d", nodes[i].inv0_gateID);
					}else{
						strcpy(input1, Abc_ObjName(nodes[nodes[i].inputs[0]].obj));
					}
				}

				// fanin1
				if (nodes[nodes[i].inputs[1]].type != PI){
					if(nodes[i].obj->fCompl1){
						nodes[i].inv1_gateID = g;

						int padding = fixed_length - 7 - (int_length(g) + strlen(inverters[nodes[i].inv1_id].name) + int_length(nodes[nodes[i].inputs[1]].nand_gateID));
						fprintf(pFile, "X%d = %s(X%d)%*s slack : %.2f\n", g++, inverters[nodes[i].inv1_id].name, nodes[nodes[i].inputs[1]].nand_gateID, padding, "", FIX_NEG_ZERO(nodes[i].inv_slack1));
						// fprintf(pFile, "X%d = %s(X%d)\t\t\tslack : %.2f\n", g++, inverters[nodes[i].inv1_id].name, nodes[nodes[i].inputs[1]].nand_gateID, nodes[i].inv_slack1);
						sprintf(input2, "X%d", nodes[i].inv1_gateID);
					}else{
						sprintf(input2, "X%d", nodes[nodes[i].inputs[1]].nand_gateID);
					}
				}
				// fanin1 is PI
				else{
					if(nodes[i].obj->fCompl1){
						nodes[i].inv1_gateID = g;

						int padding = fixed_length - 6 - (int_length(g) + strlen(inverters[nodes[i].inv1_id].name) + strlen(Abc_ObjName(nodes[nodes[i].inputs[1]].obj)));
						fprintf(pFile, "X%d = %s(%s)%*s slack : %.2f\n", g++, inverters[nodes[i].inv1_id].name, Abc_ObjName(nodes[nodes[i].inputs[1]].obj), padding, "", FIX_NEG_ZERO(nodes[i].inv_slack1));
						// fprintf(pFile, "X%d = %s(%s)\t\t\tslack : %.2f\n", g++, inverters[nodes[i].inv1_id].name, Abc_ObjName(nodes[nodes[i].inputs[1]].obj), nodes[i].inv_slack1);
						sprintf(input2, "X%d", nodes[i].inv1_gateID);
					}else{
						strcpy(input2, Abc_ObjName(nodes[nodes[i].inputs[1]].obj));
					}
				}
				nodes[i].nand_gateID = g;

				int padding_ = fixed_length - 8 - (int_length(g) + strlen(nands[nodes[i].nand_id].name) + strlen(input1) + strlen(input2));
				fprintf(pFile, "X%d = %s(%s, %s)%*s slack : %.2f\n", g++, nands[nodes[i].nand_id].name, input1, input2, padding_, "", FIX_NEG_ZERO(nodes[i].slack));
				// fprintf(pFile, "X%d = %s(%s, %s)\t\t\tslack : %.2f\n", g++, nands[nodes[i].nand_id].name, input1, input2, nodes[i].slack);
			}
		}
	}

	fclose(pFile);
}

//***********************************************************
int 
main(int argc, char **argv)
{
	char circuit[100];			// input circuit name.
	Abc_Ntk_t *ntk;

	printf("Process %s\n", argv[argc-1]);
	
	// << Setup ABC >>
	Abc_Start();
	
  	// step1: << Read blif file and library file >>
	strcpy(circuit, argv[argc-1]);
  	if (!(ntk = Io_ReadBlifAsAig(circuit, 1))) return 1; 
	parseLib();
	sortLib();

	// step2: << map AND to NAND+INV >>
	mapping(ntk);
	createnodes(ntk);
	// sortNodes();

	// step3: << calculate initial delay >>
	initialDelay();
	printf("NODE: %d INV: %d NAND: %d\n", node_count, _inv, nand);
	printf("initial_delay: %f\noriginal_area: %f\n", _initial_delay, _original_area);

	// step4: << optimize area using slack>>
	optimization();
	printf("optimized_area: %f\n", _optimized_area);

	// step5: << output >>
	char *file_name;
	file_name = strtok(argv[argc - 1], ".");
	strcat(file_name, ".mbench");
	Write(file_name, ntk);	

	// PrintEachObj(ntk);

	Abc_NtkDelete(ntk);

	// << End ABC >>
	Abc_Stop();
	
	return 1;
}
