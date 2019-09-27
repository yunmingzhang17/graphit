//
// Created by Ajay Brahmakshatriya on 9/7/2019
//

#include <graphit/backend/codegen_gpu/codegen_gpu.h>
#include <graphit/backend/codegen_gpu/assign_function_context.h>
#include "graphit/backend/codegen_gpu/extract_read_write_set.h"
#include <graphit/midend/mir.h>
#include <cstring>

namespace graphit {
int CodeGenGPU::genGPU() {
	AssignFunctionContext assign_function_context(mir_context_);
	assign_function_context.assign_function_context();


	CodeGenGPUHost code_gen_gpu_host(oss, mir_context_, module_name, "");

	genIncludeStmts();
	
	// This generates all the declarations of type GraphT<...>
	genEdgeSets();

	// Declare all the vertex properties
	// We are only declaring the device versions now. If required we can generate the host versions later
	for (auto constant: mir_context_->getLoweredConstants()) {
		if ((mir::isa<mir::VectorType>(constant->type))) {
			// This is some vertex data
			genPropertyArrayDecl(constant);	
		} else {
			assert(false && "Constant type not handled yet in GPU backend\n");	
		}
	}	
		
	std::vector<mir::FuncDecl::Ptr> functions = mir_context_->getFunctionList();
	
	// Every operator requires a kernel to be generated
	// Create that first because all the actual functions will be calling these kernels
	CodeGenGPUKernelEmitter kernel_emitter(oss, mir_context_);
	for (auto function: functions)
		function->accept(&kernel_emitter);		

	for (auto function: functions) {
		if (function->function_context & mir::FuncDecl::function_context_type::CONTEXT_DEVICE)
			function->accept(this);
		if (function->function_context & mir::FuncDecl::function_context_type::CONTEXT_HOST)
			function->accept(&code_gen_gpu_host);
	}

	oss << std::endl;
	return 0;
}
void CodeGenGPU::genPropertyArrayDecl(mir::VarDecl::Ptr constant) {
	mir::VectorType::Ptr vector_type = mir::to<mir::VectorType>(constant->type);
	vector_type->vector_element_type->accept(this);
	oss << " __device__ *" << constant->name << ";" << std::endl;

	// Also generate the host versions of these arrays 
	vector_type->vector_element_type->accept(this);
	oss << " " << "*__host_" << constant->name << ";" << std::endl;
	// Also generate the device pointer for easy copy
	vector_type->vector_element_type->accept(this);
	oss << " " << "*__device_" << constant->name << ";" << std::endl;
}

void CodeGenGPU::genPropertyArrayAlloca(mir::VarDecl::Ptr var_decl) {
	auto vector_type = mir::to<mir::VectorType>(var_decl->type);
	assert(vector_type != nullptr);
	
	auto size_expr = mir_context_->getElementCount(vector_type->element_type);
	assert(size_expr != nullptr);
	

	printIndent();
	oss << "cudaMalloc(&__device_" << var_decl->name << ", ";
	size_expr->accept(this);
	oss << " * sizeof(";
	vector_type->vector_element_type->accept(this);
	oss << "));" << std::endl;
	
	printIndent();
	oss << "cudaMemcpyToSymbol(";
	oss << var_decl->name;
	oss << ", &__device_" << var_decl->name << ", sizeof(";
	vector_type->vector_element_type->accept(this);	
	oss << "*), 0);" << std::endl;

	printIndent();
	oss << "__host_" << var_decl->name << " = new ";
	vector_type->vector_element_type->accept(this);
	oss << "[";
	size_expr->accept(this);
	oss << "];" << std::endl;
	
		
}

void CodeGenGPUKernelEmitter::genEdgeSetGlobalKernel(mir::EdgeSetApplyExpr::Ptr apply_expr) {
	std::string load_balance_function = "gpu_runtime::vertex_based_load_balance";
	if (apply_expr->applied_schedule.load_balancing == fir::gpu_schedule::SimpleGPUSchedule::load_balancing_type::TWCE) {
		load_balance_function = "gpu_runtime::TWCE_load_balance";
	}
	std::string accessor_type = "gpu_runtime::AccessorSparse";
	if (apply_expr->applied_schedule.direction == fir::gpu_schedule::SimpleGPUSchedule::direction_type::DIR_PULL && apply_expr->to_func == "")
		accessor_type = "gpu_runtime::AccessorAll";

	std::string src_filter = "gpu_runtime::true_function";
	if (apply_expr->applied_schedule.direction == fir::gpu_schedule::SimpleGPUSchedule::direction_type::DIR_PULL && apply_expr->to_func != "")
		src_filter = apply_expr->to_func;

	std::string kernel_function_name = "gpu_operator_kernel_" + mir_context_->getUniqueNameCounterString();

	oss << "template <typename EdgeWeightType>" << std::endl;
	oss << "void __global__ " << kernel_function_name << " (gpu_runtime::GraphT<EdgeWeightType> graph, gpu_runtime::VertexFrontier input_frontier, gpu_runtime::VertexFrontier output_frontier) {" << std::endl;
	indent();
	printIndent();
	oss << load_balance_function << "<EdgeWeightType, " << apply_expr->device_function << "<EdgeWeightType>, " << accessor_type << ", " << src_filter << "> (";
	oss << "graph, input_frontier, output_frontier);" << std::endl;
	
	dedent();
	printIndent();
	oss << "}" << std::endl;
	apply_expr->kernel_function = kernel_function_name;
}
void CodeGenGPUKernelEmitter::visit(mir::PushEdgeSetApplyExpr::Ptr apply_expr) {

	// First we generate the function that is passed to the load balancing function

	std::string load_balancing_arg = "gpu_operator_body_" + mir_context_->getUniqueNameCounterString();

	oss << "template <typename EdgeWeightType>" << std::endl;
	oss << "void __device__ " << load_balancing_arg << "(gpu_runtime::GraphT<EdgeWeightType> graph, int32_t src, int32_t dst, int32_t edge_id, gpu_runtime::VertexFrontier input_frontier, gpu_runtime::VertexFrontier output_frontier) {" << std::endl;
	indent();
	printIndent();
	oss << "// Body of the actual operator code" << std::endl;
	printIndent();
	oss << "EdgeWeightType weight = graph.d_edge_weight[edge_id];" << std::endl;
	printIndent();
	oss << "if (" << apply_expr->input_function_name << "(src, dst, weight)) {" << std::endl;
	indent();
	printIndent();
	if (apply_expr->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::FRONTIER_FUSED)
		oss << "gpu_runtime::enqueueVertexSparseQueue(output_frontier.d_sparse_queue_output, output_frontier.d_num_elems_output, dst);" << std::endl;
	else if (apply_expr->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::UNFUSED_BOOLMAP)
		oss << "gpu_runtime::enqueueVertexBytemap(output_frontier.d_byte_map_output, output_frontier.d_num_elems_output, dst);" << std::endl;
	else if (apply_expr->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::UNFUSED_BITMAP)
		oss << "gpu_runtime::enqueueVertexBitmap(output_frontier.d_bit_map_output, output_frontier.d_num_elems_output, dst);" << std::endl;
	dedent();
	printIndent();
	oss << "}" << std::endl;
	dedent();
	printIndent();
	oss << "}" << std::endl;	
	apply_expr->device_function = load_balancing_arg;
	genEdgeSetGlobalKernel(apply_expr);
	
}

void CodeGenGPUKernelEmitter::visit(mir::PullEdgeSetApplyExpr::Ptr apply_expr) {
	// First we generate the function that is passed to the load balancing function
	std::string load_balancing_arg = "gpu_operator_body_" + mir_context_->getUniqueNameCounterString();
	std::string load_balance_function = "gpu_runtime::vertex_based_load_balance";
	if (apply_expr->applied_schedule.load_balancing == fir::gpu_schedule::SimpleGPUSchedule::load_balancing_type::TWCE) {
		load_balance_function = "gpu_runtime::TWCE_load_balance";
	}
	
	oss << "template <typename EdgeWeightType>" << std::endl;
	oss << "void __device__ " << load_balancing_arg << "(gpu_runtime::GraphT<EdgeWeightType> graph, int32_t src, int32_t dst, int32_t edge_id, gpu_runtime::VertexFrontier input_frontier, gpu_runtime::VertexFrontier output_frontier) {" << std::endl;
	indent();
	printIndent();
	oss << "// Body of the actual operator" << std::endl;
	// Before we generate the call to the UDF, we have to check if the dst is on the input frontier
	
	printIndent();
	oss << "if (!input_frontier.d_byte_map_input[dst])" << std::endl;
	indent();
	printIndent();
	oss << "return;" << std::endl;
	dedent();

	printIndent();
	oss << "EdgeWeightType weight = graph.d_edge_weight[edge_id];" << std::endl;
	printIndent();
	// Order is reversed here because PULL direction
	oss << "if (" << apply_expr->input_function_name << "(dst, src, weight)) {" << std::endl;
	indent();
	printIndent();
	if (apply_expr->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::FRONTIER_FUSED)
		oss << "gpu_runtime::enqueueVertexSparseQueue(output_frontier.d_sparse_queue_output, output_frontier.d_num_elems_output, src);" << std::endl;
	else if (apply_expr->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::UNFUSED_BOOLMAP)
		oss << "gpu_runtime::enqueueVertexBytemap(output_frontier.d_byte_map_output, output_frontier.d_num_elems_output, src);" << std::endl;
	else if (apply_expr->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::UNFUSED_BITMAP)
		oss << "gpu_runtime::enqueueVertexBitmap(output_frontier.d_bit_map_output, output_frontier.d_num_elems_output, src);" << std::endl;
	dedent();
	printIndent();
	oss << "}" << std::endl;
	dedent();
	printIndent();
	oss << "}" << std::endl;	
	apply_expr->device_function = load_balancing_arg;
	genEdgeSetGlobalKernel(apply_expr);
}

void CodeGenGPU::genIncludeStmts(void) {
	oss << "#include \"gpu_intrinsics.h\"" << std::endl;

}

void CodeGenGPU::genEdgeSets(void) {
	for (auto edgeset: mir_context_->getEdgeSets()) {
		auto edge_set_type = mir::to<mir::EdgeSetType>(edgeset->type);
		edge_set_type->accept(this);
		oss << " " << edgeset->name << ";" << std::endl;
	}
}

void CodeGenGPU::visit(mir::EdgeSetType::Ptr edgeset_type) {
	if (edgeset_type->weight_type != nullptr) {
		oss << "gpu_runtime::GraphT<";
		edgeset_type->weight_type->accept(this);
		oss << ">";	
	} else {
		oss << "gpu_runtime::GraphT<int>";
	}
}

void CodeGenGPU::visit(mir::VertexSetType::Ptr vertexset_type) {
	oss << "gpu_runtime::VertexFrontier";
}
void CodeGenGPU::visit(mir::ScalarType::Ptr scalar_type) {
	switch(scalar_type->type) {
		case mir::ScalarType::Type::INT:
			oss << "int32_t";
			break;
		case mir::ScalarType::Type::UINT:
			oss << "uint32_t";
			break;
		case mir::ScalarType::Type::FLOAT:
			oss << "float";
			break;
		case mir::ScalarType::Type::DOUBLE:
			oss << "double";
			break;
		case mir::ScalarType::Type::BOOL:
			oss << "bool";
			break;
		case mir::ScalarType::Type::COMPLEX:
			assert(false && "Complex type not yet supported with the GPU backend\n");
			break;
		case mir::ScalarType::Type::STRING:
			assert(false && "String type not yet supported with the GPU backend\n");
			break;
		default:
			assert(false && "Invalid type enum for scalar type\n");
			break;
	}
}

void CodeGenGPU::visit(mir::FuncDecl::Ptr func_decl) {
	if (func_decl->type == mir::FuncDecl::Type::EXTERNAL) {
		assert(false && "GPU backend currently doesn't support external functions\n");
	} else {
		// First generate the signature of the function
		if (func_decl->name == "main") {
			oss << "int " << getBackendFunctionLabel() << " main(int argc, char* argv[])";
		} else {
			if (func_decl->result.isInitialized()) {
				func_decl->result.getType()->accept(this);
			} else {
				oss << "void";
			}
			oss << " " << getBackendFunctionLabel() << " " << func_decl->name << "(";
			bool printDelimeter = false;
			for (auto arg: func_decl->args) {
				if (printDelimeter)
					oss << ", ";
				arg.getType()->accept(this);
				oss << " " << arg.getName();
				printDelimeter = true;
			}
			oss << ")";	
		}
		oss << " {" << std::endl;
		indent();

		if (func_decl->name == "main") {
			for (auto stmt: mir_context_->edgeset_alloc_stmts) {
				mir::AssignStmt::Ptr assign_stmt = mir::to<mir::AssignStmt>(stmt);
				mir::EdgeSetLoadExpr::Ptr edge_set_load_expr = mir::to<mir::EdgeSetLoadExpr>(assign_stmt->expr);
				mir::VarExpr::Ptr lhs_var = mir::to<mir::VarExpr>(assign_stmt->lhs);
				std::string var_name  = lhs_var->var.getName();
				
				printIndent();
				oss << "gpu_runtime::load_graph(";
				oss << var_name << ", ";
				edge_set_load_expr->file_name->accept(this);
				oss << ", false);" << std::endl;

			}
			for (auto constant: mir_context_->getLoweredConstants()) {
				if (mir::isa<mir::VectorType>(constant->type)) {
					if (constant->needs_allocation) 
						genPropertyArrayAlloca(constant);
				} else {
					if (constant->initVal != nullptr) {
						printIndent();
						oss << constant->name << " = ";
						constant->initVal->accept(this);
						oss << ";" << std::endl;
					}
				}
			}
			for (auto stmt: mir_context_->field_vector_init_stmts) {
				stmt->accept(this);
			}
		}
		if (func_decl->body && func_decl->body->stmts) {
			if (func_decl->result.isInitialized()) {
				printIndent();
				func_decl->result.getType()->accept(this);
				oss << " " << func_decl->result.getName() << ";" << std::endl;
			}	
			func_decl->body->accept(this);	
			if (func_decl->result.isInitialized()) {
				printIndent();
				oss << "return " << func_decl->result.getName() << ";" << std::endl;
			}
		}	
		
		dedent();
		printIndent();
		oss << "}" << std::endl;
	}
}
void CodeGenGPU::visit(mir::ElementType::Ptr element_type) {
	oss << "int32_t";
}
void CodeGenGPU::visit(mir::ExprStmt::Ptr expr_stmt) {
	printIndent();
	expr_stmt->expr->accept(this);
	oss << ";" << std::endl;
}
void CodeGenGPU::visit(mir::VarExpr::Ptr var_expr) {
	oss << var_expr->var.getName();
}
void CodeGenGPU::visit(mir::AssignStmt::Ptr assign_stmt) {
	printIndent();
	assign_stmt->lhs->accept(this);
	oss << " = ";
	assign_stmt->expr->accept(this);
	oss << ";" << std::endl;
}

void CodeGenGPU::generateBinaryExpr(mir::BinaryExpr::Ptr expr, std::string token) {
	oss << "(";
	expr->lhs->accept(this);
	oss << " " << token << " ";
	expr->rhs->accept(this);
	oss << ")";
}
void CodeGenGPU::visit(mir::AddExpr::Ptr expr) {
	generateBinaryExpr(expr, "+");
}
void CodeGenGPU::visit(mir::MulExpr::Ptr expr) {
	generateBinaryExpr(expr, "*");
}
void CodeGenGPU::visit(mir::DivExpr::Ptr expr) {
	generateBinaryExpr(expr, "/");
}
void CodeGenGPU::visit(mir::SubExpr::Ptr expr) {
	generateBinaryExpr(expr, "-");
}


void CodeGenGPU::visit(mir::TensorArrayReadExpr::Ptr expr) {
	expr->target->accept(this);
	oss << "[";
	expr->index->accept(this);
	oss << "]";	
}
void CodeGenGPUHost::visit(mir::TensorArrayReadExpr::Ptr expr) {
	mir::VarExpr::Ptr var_expr = mir::to<mir::VarExpr>(expr->target);
	if (mir_context_->isLoweredConstTensor(var_expr->var.getName()))	
		oss << "__host_";
	expr->target->accept(this);
	oss << "[";
	expr->index->accept(this);
	oss << "]";
}

void CodeGenGPU::visit(mir::IntLiteral::Ptr expr) {
	oss << expr->val;
}
void CodeGenGPU::visit(mir::StringLiteral::Ptr expr) {
	oss << "\"";
	for (auto ch : expr->val)
		if (iscntrl(ch) || ch == '\\' || ch == '\"' || ch == '\'')
			oss << "\\0" << std::oct << (int)(ch);	
		else
			oss << ch;
	oss << "\"";
}
void CodeGenGPU::visit(mir::ReduceStmt::Ptr reduce_stmt) {
	switch (reduce_stmt->reduce_op_) {
		case mir::ReduceStmt::ReductionOp::SUM:
			printIndent();
			reduce_stmt->lhs->accept(this);
			oss << " += ";
			reduce_stmt->expr->accept(this);
			oss << ";" << std::endl;
			if (reduce_stmt->tracking_var_name_ != "") {
				printIndent();
				oss << reduce_stmt->tracking_var_name_ << " = true;" << std::endl;
			}
			break;
		case mir::ReduceStmt::ReductionOp::MIN:
			printIndent();
			oss << "if ((";
			reduce_stmt->lhs->accept(this);
			oss << ") > (";
			reduce_stmt->expr->accept(this);
			oss << ")) {" << std::endl;
			indent();
			printIndent();
			reduce_stmt->lhs->accept(this);
			oss << " = ";
			reduce_stmt->expr->accept(this);
			oss << ";" << std::endl;

			if (reduce_stmt->tracking_var_name_ != "") {
				printIndent();
				oss << reduce_stmt->tracking_var_name_ << " = true;" << std::endl;
			}
			dedent();
			printIndent();
			oss << "}" << std::endl;
			break;
		case mir::ReduceStmt::ReductionOp::MAX:
			printIndent();
			oss << "if ((";
			reduce_stmt->lhs->accept(this);
			oss << ") < (";
			reduce_stmt->expr->accept(this);
			oss << ")) {" << std::endl;
			indent();
			printIndent();
			reduce_stmt->lhs->accept(this);
			oss << " = ";
			reduce_stmt->expr->accept(this);
			oss << ";" << std::endl;

			if (reduce_stmt->tracking_var_name_ != "") {
				printIndent();
				oss << reduce_stmt->tracking_var_name_ << " = true;" << std::endl;
			}
			dedent();
			printIndent();
			oss << "}" << std::endl;
			break;
		case mir::ReduceStmt::ReductionOp::ATOMIC_MIN:
			printIndent();
			if (reduce_stmt->tracking_var_name_ != "") 
				oss << reduce_stmt->tracking_var_name_ << " = ";
			oss << "gpu_runtime::writeMin(&";
			reduce_stmt->lhs->accept(this);
			oss << ", ";
			reduce_stmt->expr->accept(this);
			oss << ");" << std::endl;
			break;
		case mir::ReduceStmt::ReductionOp::ATOMIC_SUM:
			if (reduce_stmt->tracking_var_name_ != "") {
				printIndent();
				oss << reduce_stmt->tracking_var_name_ << " = true;" << std::endl;
			}
			printIndent();
			oss << "writeAdd(&";
			reduce_stmt->lhs->accept(this);
			oss << ", ";
			reduce_stmt->expr->accept(this);
			oss << ");" << std::endl;
			break;
	}	
}
void CodeGenGPU::visit(mir::VarDecl::Ptr var_decl) {
	
	printIndent();
	var_decl->type->accept(this);
	
	oss << " " << var_decl->name;
	
	if (var_decl->initVal != nullptr) {
		// Special case if RHS is a EdgeSetApplyExpr
		if (mir::isa<mir::EdgeSetApplyExpr>(var_decl->initVal)) {
			mir::EdgeSetApplyExpr::Ptr esae = mir::to<mir::EdgeSetApplyExpr>(var_decl->initVal);
			if (esae->from_func == "") {
				assert(false && "GPU backend doesn't currently support creating output frontier without input frontier\n");
			}		
			// We will assume that the output frontier can reuse the input frontier. 
			// TOOD: Add liveness analysis for this
			oss << " = " << esae->from_func;
			oss << ";" << std::endl;
			printIndent();
			
			oss << "{" << std::endl;
			indent();
			std::string load_balance_function = "gpu_runtime::vertex_based_load_balance";
			if (esae->applied_schedule.load_balancing == fir::gpu_schedule::SimpleGPUSchedule::load_balancing_type::TWCE) {
				load_balance_function = "gpu_runtime::TWCE_load_balance";
			}
			
			if (mir::isa<mir::PushEdgeSetApplyExpr>(esae)) {
				printIndent();
				oss << "gpu_runtime::vertex_set_prepare_sparse(";
				oss << esae->from_func;
				oss << ");" << std::endl;
			} else if (mir::isa<mir::PullEdgeSetApplyExpr>(esae)) {
				printIndent();
				oss << "gpu_runtime::vertex_set_prepare_boolmap(";
				oss << esae->from_func;
				oss << ");" << std::endl;

				std::string to_func = esae->to_func;
				if (to_func != "") {
					printIndent();
					oss << "gpu_runtime::vertex_set_create_reverse_sparse_queue<" << to_func << ">(";
					oss << esae->from_func << ");" << std::endl;
				}
				
			}
			printIndent();
			oss << var_decl->name << " = " << esae->from_func << ";" << std::endl;
			

			printIndent();
			oss << "int32_t num_cta, cta_size;" << std::endl;

			std::string accessor_type = "gpu_runtime::AccessorSparse";
			if (esae->applied_schedule.direction == fir::gpu_schedule::SimpleGPUSchedule::direction_type::DIR_PULL && esae->to_func == "")
				accessor_type = "gpu_runtime::AccessorAll";

			printIndent();		
			oss << load_balance_function << "_info<" << accessor_type << ">(";
			oss << esae->from_func;
			oss << ", num_cta, cta_size);" << std::endl;
			printIndent();
			oss << esae->kernel_function << "<<<num_cta, cta_size>>>" << "(";
			esae->target->accept(this);
			oss << ", " << esae->from_func << ", " << var_decl->name << ");" << std::endl;
			printIndent();
			oss << "cudaDeviceSynchronize();" << std::endl;
			if (esae->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::FRONTIER_FUSED) {
				printIndent();
				oss << "gpu_runtime::swap_queues(" << var_decl->name << ");" << std::endl;
				printIndent();
				oss << var_decl->name << ".format_ready = gpu_runtime::VertexFrontier::SPARSE;" << std::endl;
			
			} else if (esae->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::UNFUSED_BITMAP) {
				printIndent();
				oss << "gpu_runtime::swap_bitmaps(" << var_decl->name << ");" << std::endl;
				printIndent();
				oss << var_decl->name << ".format_ready = gpu_runtime::VertexFrontier::BITMAP;" << std::endl;
			} else if (esae->applied_schedule.frontier_creation == fir::gpu_schedule::SimpleGPUSchedule::frontier_creation_type::UNFUSED_BOOLMAP) {
				printIndent();
				oss << "gpu_runtime::swap_bytemaps(" << var_decl->name << ");" << std::endl;
				printIndent();
				oss << var_decl->name << ".format_ready = gpu_runtime::VertexFrontier::BYTEMAP;" << std::endl;
			}
			dedent();
			printIndent();
			oss << "}" << std::endl;
		} else {
			oss << " = ";
			var_decl->initVal->accept(this);
			oss << ";" << std::endl;
		}
	} else 
		oss << ";" << std::endl;
		
	
}
void CodeGenGPU::visit(mir::VertexSetDedupExpr::Ptr vsde) {
	oss << "gpu_runtime::dedup_frontier(";
	vsde->target->accept(this);
	oss << ")";
}
void CodeGenGPU::visit(mir::BoolLiteral::Ptr bool_literal) {
	oss << bool_literal->val?"true":"false";
}
void CodeGenGPU::visit(mir::ForStmt::Ptr for_stmt) {
	printIndent();
	oss << "for (int32_t " << for_stmt->loopVar << " = ";
	for_stmt->domain->lower->accept(this);
	oss << "; " << for_stmt->loopVar << " < ";
	for_stmt->domain->upper->accept(this);
	oss << "; " << for_stmt->loopVar << "++) {" << std::endl;
	indent();
	for_stmt->body->accept(this);
	dedent();
	printIndent();
	oss << "}" << std::endl;
}
void CodeGenGPU::visit(mir::WhileStmt::Ptr while_stmt) {
	printIndent();
	oss << "while (";
	while_stmt->cond->accept(this);
	oss << ") {" << std::endl;
	indent();
	while_stmt->body->accept(this);
	dedent();
	printIndent();
	oss << "}" << std::endl;
}
void CodeGenGPU::visit(mir::IfStmt::Ptr if_stmt) {
	printIndent();
	oss << "if (";
	if_stmt->cond->accept(this);
	oss << ") {" << std::endl;
	indent();
	if_stmt->ifBody->accept(this);
	dedent();
	printIndent();
	oss << "}";
	if (if_stmt->elseBody != nullptr) {
		oss << " else {" << std::endl;
		indent();
		if_stmt->elseBody->accept(this);
		dedent();
		printIndent();
		oss << "}";
	}	
	oss << std::endl;
}
void CodeGenGPU::visit(mir::PrintStmt::Ptr print_stmt) {
	printIndent();
	oss << "std::cout << ";
	print_stmt->expr->accept(this);
	oss << " << std::endl;" << std::endl;
}
void CodeGenGPU::visit(mir::Call::Ptr call_expr) {
	if (call_expr->name == "deleteObject" || call_expr->name.substr(0, strlen("builtin_")) == "builtin_")	
		oss << "gpu_runtime::" << call_expr->name << "(";
	else
		oss << call_expr->name << "(";
	
	bool printDelimeter = false;
	for (auto arg: call_expr->args) {
		if (printDelimeter) 
			oss << ", ";
		arg->accept(this);
		printDelimeter = true;
	}	
	oss << ")";
}
void CodeGenGPU::visit(mir::EqExpr::Ptr eq_expr) {
	oss << "(";
	eq_expr->operands[0]->accept(this);
	oss << ")";

	for (unsigned i = 0; i < eq_expr->ops.size(); ++i) {
		switch(eq_expr->ops[i]) {
			case mir::EqExpr::Op::LT:
				oss << " < ";
				break;
			case mir::EqExpr::Op::LE:
				oss << " <= ";
				break;
			case mir::EqExpr::Op::GT:
				oss << " > ";
				break;
			case mir::EqExpr::Op::GE:
				oss << " >= ";
				break;
			case mir::EqExpr::Op::EQ:
				oss << " == ";
				break;
			case mir::EqExpr::Op::NE:
				oss << " != ";
				break;
			default:
				assert(false && "Invalid operator for EqExpr\n");

		}
		oss << "(";
		eq_expr->operands[i+1]->accept(this);
		oss << ")";
	}
}
void CodeGenGPU::visit(mir::BreakStmt::Ptr break_stmt) {
	printIndent();
	oss << "break;" << std::endl;
}
void CodeGenGPU::visit(mir::VertexSetApplyExpr::Ptr vsae) {
	oss << "gpu_runtime::vertex_set_apply_kernel"; 
	oss << "<" << vsae->input_function_name << ">";
	oss << "<<<NUM_CTA, CTA_SIZE>>>";
	auto mir_var = mir::to<mir::VarExpr> (vsae->target);
	if (mir_context_->isConstVertexSet(mir_var->var.getName())) {
		auto associated_element_type = mir_context_->getElementTypeFromVectorOrSetName(mir_var->var.getName());
		assert(associated_element_type != nullptr);
		auto associated_element_type_size = mir_context_->getElementCount(associated_element_type);
		assert(associated_element_type_size != nullptr);
		oss << "(";
		associated_element_type_size->accept(this);
		oss << ")";	
	} else {
		oss << "(";
		oss << mir_var->var.getName();
		oss << ")";
	}		
}
void CodeGenGPU::visit(mir::VertexSetAllocExpr::Ptr vsae) {
	mir::Expr::Ptr size_expr = mir_context_->getElementCount(vsae->element_type);
	oss << "gpu_runtime::create_new_vertex_set(";
	size_expr->accept(this);
	oss << ")" << std::endl;
}
void CodeGenGPUHost::generateDeviceToHostCopy(mir::TensorArrayReadExpr::Ptr tare) {
	printIndent();
	mir::Var target = mir::to<mir::VarExpr>(tare->target)->var;
	std::string var_name = target.getName();
	oss << "cudaMemcpy(__host_" << var_name << " + ";
	tare->index->accept(this);
	oss << ", __device_" << var_name << " + ";
	tare->index->accept(this);
	oss << ", sizeof(";
	mir::to<mir::VectorType>(target.getType())->element_type->accept(this);
	oss << "), cudaMemcpyDeviceToHost);" << std::endl;	
	
}
void CodeGenGPUHost::generateHostToDeviceCopy(mir::TensorArrayReadExpr::Ptr tare) {
	printIndent();
	mir::Var target = mir::to<mir::VarExpr>(tare->target)->var;
	std::string var_name = target.getName();
	oss << "cudaMemcpy(__device_" << var_name << " + ";
	tare->index->accept(this);
	oss << ", __host_" << var_name << " + ";
	tare->index->accept(this);
	oss << ", sizeof(";
	mir::to<mir::VectorType>(target.getType())->element_type->accept(this);
	oss << "), cudaMemcpyHostToDevice);" << std::endl;	
}
void CodeGenGPUHost::visit(mir::StmtBlock::Ptr stmt_block) {
	for (auto stmt: *(stmt_block->stmts)) {
		ExtractReadWriteSet extractor(mir_context_);
		stmt->accept(&extractor);
		for (auto tare: extractor.read_set) {
			generateDeviceToHostCopy(tare);
		}			
		stmt->accept(this);
		for (auto tare: extractor.write_set) {
			generateHostToDeviceCopy(tare);
		}
	}
}
}