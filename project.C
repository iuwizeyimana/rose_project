/*
 * A boiler plate starter file for using ROSE
 *
 * Input: sequential C/C++ code
 * Output: same C/C++ code 
 *
 */

#include "rose.h"
#include <iostream>
#include <sstream>
#include <string>
using namespace std;

#define BRAMSIZE 82944
#define BANDWIDTH 4
#define COMPM 220

//global values
int R, C, M, N, K;
int tile_indices[4]; // to be tiled tm_index, tn_index, tr_index, tc_index;
string in_name, w_name, out_name;
string in_sub, w_sub, out_sub;

typedef struct dependence_table 
{
	SgInitializedName* invarname;
	int loop_index;
	bool found_dep; //set when at least one arr has a dep. relation	
	int ub_val;
	bool is_write;	
} dep_t;
typedef struct dse_struct
{
	int comp;
	int c2c;
	int tiles[4];
	int inner; // what tiled iterator should be inner most
	// for inner and tiles 
	// 0 => tm 
	// 1 => tn 
	// 2 => tr 
	// 3 => tc 
} dse; 

int main (int argc, char *argv[]) {

  // Build a project
  SgProject *project = frontend (argc,argv);
  ROSE_ASSERT (project != NULL);

  // For each source file in the project
  SgFilePtrList & ptr_list = project->get_fileList();

  for (SgFilePtrList::iterator iter = ptr_list.begin(); 
                iter!=ptr_list.end(); iter++) { 
     SgFile* sageFile = (*iter); 
     SgSourceFile * sfile = isSgSourceFile(sageFile);
     ROSE_ASSERT(sfile);
     SgGlobal *root = sfile->get_globalScope();
     SgDeclarationStatementPtrList& declList = root->get_declarations ();

    // cout << "Found a file" << endl;

    //For each function body in the scope
     for (SgDeclarationStatementPtrList::iterator p = declList.begin(); 
              p != declList.end(); ++p) {
        SgFunctionDeclaration *func = isSgFunctionDeclaration(*p);
        if (func == 0)  continue;
        SgFunctionDefinition *defn = func->get_definition();
        if (defn == 0)  continue;
         //ignore functions in system headers, Can keep them to test robustness
        if (defn->get_file_info()->get_filename()!=sageFile->get_file_info()->get_filename())
          continue;
     //   SgBasicBlock *body = defn->get_body();  

       // cout << "Found a function" << endl;
       // For each loop 
        Rose_STL_Container<SgNode*> loops = NodeQuery::querySubTree(defn,V_SgForStatement); 
        if (loops.size()==0) continue;
	SgForStatement* loop_nest = isSgForStatement(loops[0]); //get the beginning of the loop nest
	ROSE_ASSERT(loop_nest);


	vector<dep_t> dep_table; //used for loop interchange later 

	/*************** LOOP TILING & INTERCHANGE: collect original values *******************/
	//look for all the loops that should be tiled based on a given tile size
	//categorize the loop indices based on their dependences in array access
	size_t depth_count = 0;
	for(Rose_STL_Container<SgNode*>::iterator iter =loops.begin(); iter!=loops.end(); iter++)
	{
		SgNode* current_loop = *iter;
		dep_t t;
		depth_count++;
		SgInitializedName* invarname = NULL;
		SgExpression* ub = NULL;
		if(!SageInterface::isCanonicalForLoop(current_loop,&invarname,NULL,&ub)) continue;
		t.invarname = invarname;
		t.loop_index = depth_count;
		t.found_dep = 0; //initial assumption is no dependence
		t.is_write = 0; //assume not write
		//get the value of the ub
		SgValueExp* ub_val = isSgValueExp(ub);
		int val = SageInterface::getIntegerConstantValue(ub_val); 
		if (!val) { cout << "could not change to value" << endl; continue;}
		t.ub_val = val;
		dep_table.push_back(t);
	}


	//get the body of the innermost loop before any transformations
	SgStatement* bod = NULL;
	SageInterface::isCanonicalForLoop(isSgForStatement(loops[depth_count-1]),NULL,NULL,NULL,NULL,&bod);
	ROSE_ASSERT(bod); 
	//get all the array references for dependence testing
	Rose_STL_Container<SgNode*> arrayAccess = NodeQuery::querySubTree(bod, V_SgPntrArrRefExp);
	for(Rose_STL_Container<SgNode*>::iterator it= arrayAccess.begin(); it!= arrayAccess.end(); it++ )
	{
		SgPntrArrRefExp* arr = isSgPntrArrRefExp(*it);
		SgNode* parent = arr->get_parent();
		bool is_write = 0;
		bool found_d = 0;
		if(isSgPntrArrRefExp(parent)) continue; //get to the main arry reference
		//check if the array is being written to (array for output)
		if(isSgAssignOp(parent)){
			if(isSgAssignOp(parent)->get_lhs_operand()==isSgExpression(*it)) is_write = 1;
		}
		if(isSgPlusAssignOp(parent)){
			if(isSgPlusAssignOp(parent)->get_lhs_operand()==isSgExpression(*it)) is_write = 1;
		}


		//get the array reference subscripts 
		vector<SgExpression *> *subscripts = new vector<SgExpression *>;
		SgExpression * array_name_exp = NULL;
		SageInterface::isArrayReference(arr, &array_name_exp, &subscripts);
		SgInitializedName* array_name = SageInterface::convertRefToInitializedName(array_name_exp);
		string ar_name = array_name->get_symbol_from_symbol_table()->get_name().getString();
		string ar_sub = "";
		for(vector<SgExpression*>::iterator t=(*subscripts).begin(); t !=(*subscripts).end(); t++)
		{
			//get subscript symbols;
			SgExpression* sub = isSgExpression(*t);
			if(isSgIntVal(sub)) continue;
			if(isSgAddOp(sub) || isSgSubtractOp(sub))
			{	SgExpression* lhs = isSgAddOp(sub)->get_lhs_operand();
				SgExpression* rhs = isSgAddOp(sub)->get_rhs_operand();
				if(isSgIntVal(rhs) || isSgIntVal(lhs)) continue;
				// there is a dependence, find the loops that have that dependence
				SgInitializedName* lhs_name = SageInterface::convertRefToInitializedName(lhs);
				SgInitializedName* rhs_name = SageInterface::convertRefToInitializedName(rhs);
				ar_sub += "[" + lhs_name->get_symbol_from_symbol_table()->get_name().getString() + "+" ;
				ar_sub += rhs_name->get_symbol_from_symbol_table()->get_name().getString() + "]";
				for(vector<dep_t>::iterator ti=dep_table.begin(); ti!=dep_table.end(); ti++)
				{
					dep_t a= *ti;
					if(a.invarname == lhs_name || a.invarname == rhs_name)
					{
						ti->found_dep = 1;
						found_d = 1;
						if (!ti->is_write) ti->is_write= is_write;
					}
				}
			}
			else // normal subscript, update its is_write if needed
			{
				SgInitializedName* sub_name = SageInterface::convertRefToInitializedName(sub);
				ar_sub += "[" + sub_name->get_symbol_from_symbol_table()->get_name().getString() + "]";
				for(vector<dep_t>::iterator ti=dep_table.begin(); ti!=dep_table.end(); ti++)
				{
					dep_t a= *ti;
					if(a.invarname == sub_name)
					{
						if (!ti->is_write) ti->is_write= is_write;
					}
				}
				

			}	

		}

		//get the names of the input output and weight arrays	
		if(is_write){ out_name = ar_name; out_sub = ar_sub; }
		else if(found_d){ in_name = ar_name; in_sub = ar_sub;}
		else{ w_name = ar_name; w_sub = ar_sub;}
	} //end of array access for loop
//	cout << "out: " << out_sub << " in: " << in_sub << " w: " << w_sub << endl;
	bool count = 0; 
	for(vector<dep_t>::iterator ti=dep_table.begin(); ti!=dep_table.end(); ti++)
	{
		dep_t a= *ti;
		if((a.found_dep & !a.is_write)) K = a.ub_val;
		if((!a.found_dep & a.is_write))	{ M = a.ub_val; tile_indices[0] = a.loop_index; }
		if((!a.found_dep & !a.is_write)){ N = a.ub_val; tile_indices[1] = a.loop_index; }
		if((a.found_dep & a.is_write & !count))
		{	count = 1;
			R = a.ub_val;
			tile_indices[2] = a.loop_index;
		}
		else if((a.found_dep & a.is_write & count)){ C= a.ub_val; tile_indices[3]= a.loop_index;}

	}

	/***************** LOOP TILING: Perform the tiling ********************************/
	// 1. Do the DSE 
	dse max_comp;
	max_comp.comp = 0;
	//got through every tile size 
	for(int tr = (R<3)? R:3; tr<R; tr++){
	  for(int tc= (C<3)? C:3; tc<C; tc++){
	    for(int tm = (M<3)? M:3; tm<M; tm++){
	      for(int tn= (N<3)? N:3; tn<N; tn++){
		 int comp = (2*R*C*M*N*K*K)/ ((M/tm)*(N/tn)*R*C*K*K);
		 int b_in = tn*(tr+K-1)*(tc+K-1);
		 int b_wgt = tm*tn*K*K;
		 int b_out = tm*tr*tc;
		 if( (b_in+b_wgt+b_out)>BRAMSIZE) continue;
		 int inner = 0;
		 int best_c2c = 0;
		 // a. tm inner
		 int d_out = (M/tm)*(N/tn)*(R/tr)*(C/tc);
		 int d_in = (N/tn)*(R/tr)*(C/tc);
		 int d_w = (M/tm)*(N/tn)*(R/tr)*(C/tc);
		 int c2c = (2*R*C*M*N*K*K)/((d_in*b_in)+(d_out*b_out)+(d_w*b_wgt));
		 best_c2c = c2c;
		 // b. tn inner 
		 d_out = (M/tm)*(R/tr)*(C/tc);
		 d_in = (M/tm)*(N/tn)*(R/tr)*(C/tc);
		 d_w = d_in;
		 c2c = (2*R*C*M*N*K*K)/((d_in*b_in)+(d_out*b_out)+(d_w*b_wgt));
		 if(c2c>best_c2c) { best_c2c = c2c; inner = 1;}
		 // c. tr inner
		 d_out = (M/tm)*(N/tn)*(R/tr)*(C/tc);
		 d_in = (N/tn)*(R/tr)*(C/tc);
		 d_w = (M/tm)*(N/tn)*(C/tc);
		 c2c = (2*R*C*M*N*K*K)/((d_in*b_in)+(d_out*b_out)+(d_w*b_wgt));
		 if(c2c>best_c2c) { best_c2c = c2c; inner = 2;}
		 // c. tc inner
		 d_out = (M/tm)*(N/tn)*(R/tr)*(C/tc);
		 d_in = (N/tn)*(R/tr)*(C/tc);
		 d_w = (M/tm)*(N/tn)*(R/tr);
		 c2c = (2*R*C*M*N*K*K)/((d_in*b_in)+(d_out*b_out)+(d_w*b_wgt));
		 if(c2c>best_c2c) { best_c2c = c2c; inner = 3;}
		 if (max_comp.comp == 0)
		 {
			max_comp.comp = comp;
			max_comp.c2c = best_c2c;
			max_comp.inner = inner;
			max_comp.tiles[0] = tm;
			max_comp.tiles[1] = tn;
			max_comp.tiles[2] = tr;
			max_comp.tiles[3] = tc;
		 }
		 else if((comp>max_comp.comp) && ((comp/best_c2c)<BANDWIDTH) && (comp<COMPM))
		 {
			max_comp.comp = comp;
			max_comp.c2c = best_c2c;
			max_comp.inner = inner;
			max_comp.tiles[0] = tm;
			max_comp.tiles[1] = tn;
			max_comp.tiles[2] = tr;
			max_comp.tiles[3] = tc;
		 }
		 else if((comp==max_comp.comp) && (best_c2c>max_comp.c2c))
		 {
			max_comp.comp = comp;
			max_comp.c2c = best_c2c;
			max_comp.inner = inner;
			max_comp.tiles[0] = tm;
			max_comp.tiles[1] = tn;
			max_comp.tiles[2] = tr;
			max_comp.tiles[3] = tc;
		 }

	    }
       	   }
	 }
	} //end of dse loop

	cout << "best c2c" << max_comp.c2c << endl;
	// 2. tile every other loop but the inner loop
	// inner loop tiled last to ensure it appear in the inner-most tile
	int ub[4] = {M, N, R, C}; // holds the ub for smooter programing
	for (int i=0; i<4; i++)
	{
		if(i == max_comp.inner) continue;
		if(max_comp.tiles[i] >= ub[i]) continue; // cannot have tilesize >= UB (means nothing is tiled)		
		SageInterface::loopTiling(loop_nest, tile_indices[i], max_comp.tiles[i]);

	}
 
	//3. now tile the inner loop
	if(max_comp.tiles[max_comp.inner]<ub[max_comp.inner])
		SageInterface::loopTiling(loop_nest, tile_indices[max_comp.inner], max_comp.tiles[max_comp.inner]);
	//cout << max_comp.comp <<" " <<  max_comp.c2c <<" " << max_comp.inner <<" " << max_comp.tiles[0] << " " <<max_comp.tiles[1] <<" " << max_comp.tiles[2] << " " <<max_comp.tiles[3] << endl ;
		
	/****************** LOOP INTECHANGE: Perform the interchange **********************/
	//go through the dep_table  in reverse and get loops with no dep 
	//interchange them to be inner 	
	for(vector<dep_t>::iterator ti=dep_table.end()-1; ti!=dep_table.begin()-1; ti--)
	{
		dep_t a = *ti;
		if (a.found_dep) continue;
		//hard coded
		//TODO: find a better solution
		switch(a.loop_index)
		{
			case 1: 
				SageInterface::loopInterchange(loop_nest, 3, 3);
				SageInterface::loopInterchange(loop_nest, 6, 9);
				break;
			case 2:
				SageInterface::loopInterchange(loop_nest, 4, 3);
				SageInterface::loopInterchange(loop_nest, 6, 3);
				break;
			case 3:
				SageInterface::loopInterchange(loop_nest, 6, 9);
				break;
			case 4:
				SageInterface::loopInterchange(loop_nest, 6, 3);
				break;
			case 5:
				SageInterface::loopInterchange(loop_nest, 6, 1);
				break;
			default: break;
		}

	}
	//get the loop structure after tiling/interchange
        Rose_STL_Container<SgNode*> loops_ = NodeQuery::querySubTree(defn,V_SgForStatement); 
	
	/****************** Pragma Insertion: add the unroll and dataflow pragmas *********/
	
	//the two inner loops are unrolled
	stringstream ss;  
	string str;
	ss << max_comp.tiles[1];  
  	ss >> str;
	string unroll_pragma = "#pragma HLS UNROLL factor = " + str ;
	SgForStatement* inner_loop = isSgForStatement(loops[5]); //used to put before 2nd loopnest
	SgPntrArrRefExp* arr = isSgPntrArrRefExp(arrayAccess[0]);//used to put inside 1st loopnest
	SageInterface::attachArbitraryText(inner_loop, unroll_pragma, PreprocessingInfo::before);
	stringstream ss_;
	string str2;
	ss_ << max_comp.tiles[0];  
  	ss_ >> str2;
	unroll_pragma = "#pragma HLS UNROLL factor = " + str2 ;
	SageInterface::attachArbitraryText(arr, unroll_pragma, PreprocessingInfo::before);
	//add the dataflow pragma inside the inner loop nest for tiles
	string dtflow_pragma("#pragma HLS DATAFLOW");
	SageInterface::attachArbitraryText(loop_nest, dtflow_pragma, PreprocessingInfo::before);


	/*******************  Add the load weight unit *************************************/
	

	// 1) build the array to hold the weights buffer
	// 1.a build the array type
	// 1.b build the array declaration
	// Note: we build last dimension and make our way to the first one	
	
	//make sure you are doing mem up where necessary
	if(max_comp.inner>1) loop_nest = isSgForStatement(loops_[3]); 
	SgScopeStatement* scope = loop_nest->get_scope(); //get scope
	//addition: before anything make sure to insert the label stmt
	SgLabelStatement *label_stmt = SageBuilder::buildLabelStatement("load_w");
	SageInterface::insertStatementBefore(loop_nest, label_stmt);
	// get last dimension
	SgExpression * s = SageBuilder::buildIntVal(K);
	SgArrayType* weight = SageBuilder::buildArrayType(SageBuilder::buildIntType(), s);
	SgVariableDeclaration* decl = SageBuilder::buildVariableDeclaration("w_buff", weight, NULL, scope);
	// get 3rd dimension	
	s = SageBuilder::buildIntVal(K);
	SgExprListExp* exp = SageBuilder::buildExprListExp(s);
	weight = SageBuilder::buildArrayType(weight, exp);
	decl = SageBuilder::buildVariableDeclaration("w_buff", weight, NULL, scope);
	//get 2nd dimension
	int size = max_comp.tiles[1];
	s = SageBuilder::buildIntVal(size);
	exp = SageBuilder::buildExprListExp(s);
	weight = SageBuilder::buildArrayType(weight, exp);
	decl = SageBuilder::buildVariableDeclaration("w_buff", weight, NULL, scope);
	//get 1st dimension
	size = max_comp.tiles[0];
	s = SageBuilder::buildIntVal(size);
	exp = SageBuilder::buildExprListExp(s);
	weight = SageBuilder::buildArrayType(weight, exp);
	decl = SageBuilder::buildVariableDeclaration("w_buff", weight, NULL, scope);
	ROSE_ASSERT(decl);
	SageInterface::insertStatementBefore(loop_nest,decl);
	
	//2. build the temp value for the weight
	decl = SageBuilder::buildVariableDeclaration("hold_w", SageBuilder::buildIntType(), NULL, scope);
	ROSE_ASSERT(decl);
	SageInterface::insertStatementBefore(loop_nest,decl);

	//3. build the 4 nested loop for storing values in the w_buffer
	SgVariableDeclaration* stmt1 = SageBuilder::buildVariableDeclaration("d", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(d = 0;...)
	SgStatement* init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("d", scope),SageBuilder::buildIntVal(0)); 
	//for(...; d<K;...)
	SgStatement* cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("d",scope),SageBuilder::buildIntVal(K))); 
	//for (...;...; d++)
	SgExpression* incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("d", scope),SgUnaryOp::postfix); 
	 SgForStatement* for_loop = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());

	stmt1 = SageBuilder::buildVariableDeclaration("c", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(c = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("c", scope),SageBuilder::buildIntVal(0)); 
	//for(...; c<K;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("c",scope),SageBuilder::buildIntVal(K))); 
	//for (...;...; c++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("c", scope),SgUnaryOp::postfix); 
	 SgForStatement* for_loop_2 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
	
	
	stmt1 = SageBuilder::buildVariableDeclaration("b", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(b = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("b", scope),SageBuilder::buildIntVal(0)); 
	//for(...; b<Tn;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("b",scope),SageBuilder::buildIntVal(max_comp.tiles[1]))); 
	//for (...;...; b++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("b", scope),SgUnaryOp::postfix);
	 SgForStatement* for_loop_3 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
 
	stmt1 = SageBuilder::buildVariableDeclaration("a", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(a = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("a", scope),SageBuilder::buildIntVal(0)); 
	//for(...; a<Tn;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("a",scope),SageBuilder::buildIntVal(max_comp.tiles[0]))); 
	//for (...;...; a++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("a", scope),SgUnaryOp::postfix); 
	 SgForStatement* for_loop_4 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
	SageInterface::insertStatementBefore(loop_nest, for_loop_4);
	SageInterface::appendStatement(for_loop_3,isSgBasicBlock(for_loop_4->get_loop_body()));
	SageInterface::appendStatement(for_loop_2,isSgBasicBlock(for_loop_3->get_loop_body()));
	SageInterface::appendStatement(for_loop,isSgBasicBlock(for_loop_2->get_loop_body()));

	//4. add the inner part of the load weight loops
	string inner_bod = "if(!"+w_name+".empty()){"+w_name+">> hold_w; w_buff[a][b][c][d] = hold_w;}";
	SageInterface::attachArbitraryText(isSgBasicBlock(for_loop->get_loop_body()), inner_bod, PreprocessingInfo::inside);

	/*******************  Add the load input unit *************************************/

	if(max_comp.inner == 1) loop_nest = isSgForStatement(loops_[3]); 
	else loop_nest = isSgForStatement(loops_[4]);
	scope = loop_nest->get_scope(); //get scope

	//0) insert the label stmt
	label_stmt = SageBuilder::buildLabelStatement("load_in");
	SageInterface::insertStatementBefore(loop_nest, label_stmt);
	// 1) build the array to hold the input buffer
	// 1.a build the array type
	// 1.b build the array declaration
	// Note: we build last dimension and make our way to the first one	
	// get last dimension
	s = SageBuilder::buildIntVal(max_comp.tiles[3]);
	SgArrayType* in = SageBuilder::buildArrayType(SageBuilder::buildIntType(), s);
	decl = SageBuilder::buildVariableDeclaration("in_buff", in, NULL, scope);
	//get 2nd dimension
	size = max_comp.tiles[2];
	s = SageBuilder::buildIntVal(size);
	exp = SageBuilder::buildExprListExp(s);
	in = SageBuilder::buildArrayType(in, exp);
	decl = SageBuilder::buildVariableDeclaration("in_buff", in, NULL, scope);
	//get 1st dimension
	size = max_comp.tiles[1];
	s = SageBuilder::buildIntVal(size);
	exp = SageBuilder::buildExprListExp(s);
	in = SageBuilder::buildArrayType(in, exp);
	decl = SageBuilder::buildVariableDeclaration("in_buff", in, NULL, scope);
	ROSE_ASSERT(decl);
	SageInterface::insertStatementBefore(loop_nest,decl);
	
	//2. build the temp value for the input
	decl = SageBuilder::buildVariableDeclaration("hold_in", SageBuilder::buildIntType(), NULL, scope);
	ROSE_ASSERT(decl);
	SageInterface::insertStatementBefore(loop_nest,decl);

	//3. build the 3 nested loops for storing values in the w_buffer
	stmt1 = SageBuilder::buildVariableDeclaration("c1", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(c = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("c1", scope),SageBuilder::buildIntVal(0)); 
	//for(...; c<K;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("c1",scope),SageBuilder::buildIntVal(max_comp.tiles[3]))); 
	//for (...;...; c++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("c1", scope),SgUnaryOp::postfix); 
	 for_loop_2 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
	
	
	stmt1 = SageBuilder::buildVariableDeclaration("b1", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(b = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("b1", scope),SageBuilder::buildIntVal(0)); 
	//for(...; b<Tn;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("b1",scope),SageBuilder::buildIntVal(max_comp.tiles[2]))); 
	//for (...;...; b++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("b1", scope),SgUnaryOp::postfix);
	 for_loop_3 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
 
	stmt1 = SageBuilder::buildVariableDeclaration("a1", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementBefore(loop_nest,stmt1);
	// for(a = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("a1", scope),SageBuilder::buildIntVal(0)); 
	//for(...; a<Tn;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("a1",scope),SageBuilder::buildIntVal(max_comp.tiles[1]))); 
	//for (...;...; a++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("a1", scope),SgUnaryOp::postfix); 
	 for_loop_4 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
	SageInterface::insertStatementBefore(loop_nest, for_loop_4);
	SageInterface::appendStatement(for_loop_3,isSgBasicBlock(for_loop_4->get_loop_body()));
	SageInterface::appendStatement(for_loop_2,isSgBasicBlock(for_loop_3->get_loop_body()));

	//4. add the inner part of the load weight loops
	inner_bod = "if(!"+in_name+".empty()){"+in_name+">> hold_in; in_buff[a1][b1][c1] = hold_in;}";
	SageInterface::attachArbitraryText(isSgBasicBlock(for_loop_2->get_loop_body()), inner_bod, PreprocessingInfo::inside);
	
	/*******************  Add the load output unit *************************************/

	if(max_comp.inner == 0) loop_nest = isSgForStatement(loops_[3]); 
	else loop_nest = isSgForStatement(loops_[4]);
	scope = loop_nest->get_scope(); //get scope
	// 1) build the array to hold the output buffer
	// 1.a build the array type
	// 1.b build the array declaration
	// Note: we build last dimension and make our way to the first one	
	// get last dimension
	s = SageBuilder::buildIntVal(max_comp.tiles[3]);
	SgArrayType* out= SageBuilder::buildArrayType(SageBuilder::buildIntType(), s);
	decl = SageBuilder::buildVariableDeclaration("out_buff", out, NULL, scope);
	//get 2nd dimension
	size = max_comp.tiles[2];
	s = SageBuilder::buildIntVal(size);
	exp = SageBuilder::buildExprListExp(s);
	out = SageBuilder::buildArrayType(out, exp);
	decl = SageBuilder::buildVariableDeclaration("out_buff", out, NULL, scope);
	//get 1st dimension
	size = max_comp.tiles[0];
	s = SageBuilder::buildIntVal(size);
	exp = SageBuilder::buildExprListExp(s);
	out = SageBuilder::buildArrayType(out, exp);
	decl = SageBuilder::buildVariableDeclaration("out_buff", out, NULL, scope);
	ROSE_ASSERT(decl);
	SageInterface::insertStatementBefore(loop_nest,decl);
	
	//2. build the temp value for the input - not needed here
//	decl = SageBuilder::buildVariableDeclaration("hold_out", SageBuilder::buildIntType(), NULL, scope);
//	ROSE_ASSERT(decl);
//	SageInterface::insertStatementAfter(loop_nest,decl);

	//3. build the 3 nested loops for storing values in the w_buffer
	stmt1 = SageBuilder::buildVariableDeclaration("c2", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementAfter(loop_nest,stmt1);
	// for(c = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("c2", scope),SageBuilder::buildIntVal(0)); 
	//for(...; c<K;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("c2",scope),SageBuilder::buildIntVal(max_comp.tiles[3]))); 
	//for (...;...; c++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("c2", scope),SgUnaryOp::postfix); 
	 for_loop_2 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
	
	
	SgVariableDeclaration* stmt2 = SageBuilder::buildVariableDeclaration("b2", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementAfter(stmt1,stmt2);
	// for(b = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("b2", scope),SageBuilder::buildIntVal(0)); 
	//for(...; b<Tn;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("b2",scope),SageBuilder::buildIntVal(max_comp.tiles[2]))); 
	//for (...;...; b++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("b2", scope),SgUnaryOp::postfix);
	 for_loop_3 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
 
	SgVariableDeclaration* stmt3 = SageBuilder::buildVariableDeclaration("a2", SageBuilder::buildIntType(), NULL, scope);
	SageInterface::insertStatementAfter(stmt2,stmt3);
	// for(a = 0;...)
	init_stmt = SageBuilder::buildAssignStatement(SageBuilder::buildVarRefExp("a2", scope),SageBuilder::buildIntVal(0)); 
	//for(...; a<Tn;...)
	cond_stmt = SageBuilder::buildExprStatement(SageBuilder::buildLessThanOp(SageBuilder::buildVarRefExp("a2",scope),SageBuilder::buildIntVal(max_comp.tiles[0]))); 
	//for (...;...; a++)
	incr_exp = SageBuilder::buildPlusPlusOp(SageBuilder::buildVarRefExp("a2", scope),SgUnaryOp::postfix); 
	 for_loop_4 = SageBuilder::buildForStatement(init_stmt, cond_stmt,incr_exp, SageBuilder::buildBasicBlock());
	SageInterface::insertStatementAfter(stmt3, for_loop_4);
	SageInterface::appendStatement(for_loop_3,isSgBasicBlock(for_loop_4->get_loop_body()));
	SageInterface::appendStatement(for_loop_2,isSgBasicBlock(for_loop_3->get_loop_body()));

	//4. add the inner part of the load weight loops
	inner_bod = out_name+"<< out_buff[a2][b2][c2];";
	SageInterface::attachArbitraryText(isSgBasicBlock(for_loop_2->get_loop_body()), inner_bod, PreprocessingInfo::inside);

	//0) insert the label stmt
	label_stmt = SageBuilder::buildLabelStatement("store_out");
	SageInterface::insertStatementAfter(loop_nest, label_stmt);
	//add the compute label after you performed all the loads
	loop_nest = isSgForStatement(loops_[4]);
	label_stmt = SageBuilder::buildLabelStatement("compute");
	SageInterface::insertStatementBefore(loop_nest, label_stmt);


	/****************  Change the inner compute to use buffers instead **********************/

	//could not figure out how to delete the interior code so decided to comment it out
	inner_bod = "/* ";
	SageInterface::attachArbitraryText(arr, inner_bod, PreprocessingInfo::before);
	// add the new statement 
	inner_bod = "*/ out_buff" + out_sub + " += w_buff" + w_sub + " * in_buff"+ in_sub + ";";
	SageInterface::attachArbitraryText(isSgBasicBlock(inner_loop->get_loop_body()), inner_bod, PreprocessingInfo::inside);
	
	/**************** Change the paramenters in the function declaration *****************/

	//get the parameters in the function
	//SgFunctionParameterList * this_list= func->get_parameterList();
	
	//1. get the name of the function
	string func_name = func->get_name().getString();
	//2. comment out the old function definition  
        SgBasicBlock *body = defn->get_body(); 
	string inc = "#include \"hls_stream.h\" "; 
	SageInterface::attachArbitraryText(defn,inc, PreprocessingInfo::before);
	SageInterface::attachArbitraryText(defn, "/*", PreprocessingInfo::before);
	SageInterface::attachArbitraryText(body, "*/", PreprocessingInfo::before);
	//3. write the new function definition
	string df = "void "+ func_name + "(hls::stream<int>& " + out_name + ", hls::stream<int>& " + w_name + ", hls::stream<int>& "+ in_name + ")"; 
	SageInterface::attachArbitraryText(body, df, PreprocessingInfo::before);
	cout << "inner: " << max_comp.inner << endl;
      } // end for-loop for declarations
   } //end for-loop for files

     AstTests::runAllTests(project);  
  // Generate the source code
  return backend (project);
}

