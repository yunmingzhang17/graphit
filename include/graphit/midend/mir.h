//
// Created by Yunming Zhang on 2/8/17.
//

#ifndef GRAPHIT_MIR_H
#define GRAPHIT_MIR_H

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <unordered_set>
#include <graphit/midend/mir_visitor.h>
#include <graphit/midend/mir_metadata.h>
#include <graphit/midend/var.h>
#include <assert.h>
#include <graphit/midend/field_vector_property.h>
#include <unordered_map>
#include <graphit/frontend/gpu_schedule.h>


namespace graphit {
    namespace mir {

        struct MIRNode;

        template<typename T>
        inline bool isa(std::shared_ptr<MIRNode> ptr) {
            return (bool) std::dynamic_pointer_cast<T>(ptr);
        }

        template<typename T>
        inline const std::shared_ptr<T> to(std::shared_ptr<MIRNode> ptr) {
            std::shared_ptr<T> ret = std::dynamic_pointer_cast<T>(ptr);
            assert(ret != nullptr);
            return ret;
        }

        struct MIRNode : public std::enable_shared_from_this<MIRNode> {
            typedef std::shared_ptr<MIRNode> Ptr;

            MIRNode() {}

            /** We use the visitor pattern to traverse MIR nodes throughout the
            * compiler, so we have a virtual accept method which accepts
            * visitors.
            */

            virtual void accept(MIRVisitor *) = 0;

            friend std::ostream &operator<<(std::ostream &, MIRNode &);

            template<typename T = MIRNode>
            std::shared_ptr<T> clone() {
                return to<T>(cloneNode());
            }

            // We use a single map to hold all metadata on the MIR Node
            std::unordered_map<std::string, std::shared_ptr<MIRMetadata>> metadata_map;
        protected:
            template<typename T = MIRNode>
            std::shared_ptr<T> self() {
                return to<T>(shared_from_this());
            }

            virtual void copy(MIRNode::Ptr) {};

            virtual MIRNode::Ptr cloneNode() {
                //TODO: change this to an abstract method =0
                // Right now, I just need to prevent everything blow up
                // as I slowly add in support for copy functionalities
                return nullptr;
            };
        public:
            // Functions to set and retrieve metadata of different types
            template<typename T>
            void setMetadata(std::string mdname, T val) {
                typename MIRMetadataImpl<T>::Ptr mdnode = std::make_shared<MIRMetadataImpl<T>>(val);
                metadata_map[mdname] = mdnode;
            }
            // This function is safe to be called even if the metadata with
            // the specified name doesn't exist
            template<typename T>
            bool hasMetadata(std::string mdname) {
                if (metadata_map.find(mdname) == metadata_map.end())
		    return false;
                typename MIRMetadata::Ptr mdnode = metadata_map[mdname];
                if (!mdnode->isa<T>())
                    return false;
                return true;
            }
            // This function should be called only after confirming that the 
            // metadata with the given name exists
            template <typename T>
            T getMetadata(std::string mdname) {
                if (!hasMetadata<T>(mdname)) {
                  std::cout << "No metadata for name: " << mdname.c_str() << std::endl;
                  assert(false);
                }
                typename MIRMetadata::Ptr mdnode = metadata_map[mdname];
                return mdnode->to<T>()->val;
            }

            bool hasApplySchedule() {
              return this->hasMetadata<fir::abstract_schedule::ScheduleObject::Ptr>("apply_schedule");
            }

            template<typename T = fir::abstract_schedule::ScheduleObject>
            std::shared_ptr<T> getApplySchedule() {
              return this->getMetadata<fir::abstract_schedule::ScheduleObject::Ptr>("apply_schedule")->self<T>();
            }
        };

        struct Expr : public MIRNode {
            typedef std::shared_ptr<Expr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<Expr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct StringLiteral : public Expr {
            typedef std::shared_ptr<StringLiteral> Ptr;
            std::string val;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<StringLiteral>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct IntLiteral : public Expr {
            typedef std::shared_ptr<IntLiteral> Ptr;
            int val = 0;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<IntLiteral>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct BoolLiteral : public Expr {
            typedef std::shared_ptr<BoolLiteral> Ptr;
            bool val = false;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<BoolLiteral>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct FloatLiteral : public Expr {
            typedef std::shared_ptr<FloatLiteral> Ptr;
            float val = 0;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<FloatLiteral>());
            }


        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct Stmt : public MIRNode {
            typedef std::shared_ptr<Stmt> Ptr;
            std::string stmt_label;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<Stmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct StmtBlock : public Stmt {
            std::vector<Stmt::Ptr> *stmts;

            typedef std::shared_ptr<StmtBlock> Ptr;

//            StmtBlock(){}

//            ~StmtBlock(){
//                if(stmts != nullptr) delete stmts;
//            }

            void insertStmtEnd(mir::Stmt::Ptr stmt) {
                if (stmts == nullptr)
                    stmts = new std::vector<mir::Stmt::Ptr>();
                stmts->push_back(stmt);
            }

            void insertStmtFront(mir::Stmt::Ptr stmt) {
                if (stmts == nullptr)
                    stmts = new std::vector<mir::Stmt::Ptr>();
                stmts->insert(stmts->begin(), stmt);
            }

            void insertStmtBlockFront(mir::StmtBlock::Ptr stmt_block) {

                if (stmt_block->stmts == nullptr) {
                    return;
                }

                if (stmts == nullptr) {
                    stmts = new std::vector<mir::Stmt::Ptr>();
                }

                if (stmts != nullptr && stmt_block->stmts != nullptr) {
                    int num_stmts = stmt_block->stmts->size();
                    for (int i = 0; i < num_stmts; i++) {
                        insertStmtFront((*(stmt_block->stmts))[num_stmts - 1 - i]);
                    }
                }

            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<StmtBlock>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct Type : public MIRNode {
            typedef std::shared_ptr<Type> Ptr;
        };

        struct ScalarType : public Type {
            enum class Type {
                INT, UINT, FLOAT, DOUBLE, BOOL, COMPLEX, STRING
            };
            Type type;
            typedef std::shared_ptr<ScalarType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ScalarType>());
            }

	    enum class BoolType {
		BYTE, BIT
	    };
	    BoolType bool_type;

            std::string toString(){
                std::string output_str = "";
                if (type == mir::ScalarType::Type::FLOAT){
                    output_str = "float";
                } else if (type == mir::ScalarType::Type::INT){
                    output_str = "int";
                } else if (type == mir::ScalarType::Type::BOOL){
                    output_str = "bool";
                } else if (type == mir::ScalarType::Type::DOUBLE){
                    output_str = "double";
                } else if (type == mir::ScalarType::Type::STRING){
                    output_str = "string";
                }
                return output_str;
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct ElementType : public Type {
            std::string ident;
            typedef std::shared_ptr<ElementType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ElementType>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct VectorType : public Type {
            // optional, used for element field / system vectors
            ElementType::Ptr element_type;
            // scalar type for each element of the vector (not the global Elements)
            Type::Ptr vector_element_type;
            int range_indexset = 0;
            std::string typedef_name_ = "";

            std::string toString(){
                std::string output_str = "";
                if (mir::isa<mir::ScalarType>(vector_element_type)){
                    auto scalar_type = mir::to<mir::ScalarType>(vector_element_type);
                    output_str = output_str + scalar_type->toString();
                }

                output_str = output_str + std::to_string(range_indexset);
                return output_str;
            }

            typedef std::shared_ptr<VectorType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VectorType>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        // OG Additions - moved here because VertexSetType needs it too
        enum PriorityUpdateType {
            NoPriorityUpdate, //default type
            EagerPriorityUpdate, // GAPBS refactored runtime lib
            EagerPriorityUpdateWithMerge, // GAPBS refactored runtime lib
            ConstSumReduceBeforePriorityUpdate, //Julienne refactored runtime lib
            ReduceBeforePriorityUpdate, //Julienne refactored runtime lib
	        ExternPriorityUpdate, // Julienne refactored runtime lib
        };
        struct VertexSetType : public Type {
            ElementType::Ptr element;

            typedef std::shared_ptr<VertexSetType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VertexSetType>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct ListType : public Type {
            Type::Ptr element_type;

            typedef std::shared_ptr<ListType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ListType>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct EdgeSetType : public ElementType {
            ElementType::Ptr element;
            ScalarType::Ptr weight_type;
            std::vector<ElementType::Ptr> *vertex_element_type_list;

            EdgeSetType() {
              this->setMetadata<mir::PriorityUpdateType>("priority_update_type", mir::PriorityUpdateType::NoPriorityUpdate);
            }

            typedef std::shared_ptr<EdgeSetType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<EdgeSetType>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct ForDomain : public MIRNode {
            Expr::Ptr lower;
            Expr::Ptr upper;

            typedef std::shared_ptr<ForDomain> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ForDomain>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct NameNode : public Stmt {
            StmtBlock::Ptr body;
            typedef std::shared_ptr<NameNode> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<NameNode>());
            }


        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct ForStmt : public Stmt {
            std::string loopVar;
            ForDomain::Ptr domain;
            StmtBlock::Ptr body;

            typedef std::shared_ptr<ForStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ForStmt>());
            }


        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct WhileStmt : public Stmt {
            Expr::Ptr cond;
            StmtBlock::Ptr body;

            typedef std::shared_ptr<WhileStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<WhileStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct ExprStmt : public Stmt {
            Expr::Ptr expr;

            typedef std::shared_ptr<ExprStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ExprStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct AssignStmt : public ExprStmt {
            //TODO: do we really need a vector??
            //std::vector<Expr::Ptr> lhs;
            Expr::Ptr lhs;

            typedef std::shared_ptr<AssignStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<AssignStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct ReduceStmt : public AssignStmt {
            enum class ReductionOp {
                MIN, SUM, MAX, ATOMIC_MIN, ATOMIC_MAX, ATOMIC_SUM
            };
            ReductionOp reduce_op_;

            typedef std::shared_ptr<ReduceStmt> Ptr;

            ReduceStmt() {
              this->setMetadata<bool>("is_atomic_", false);
            }
            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ReduceStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();


        };





        struct CompareAndSwapStmt : public AssignStmt {
            Expr::Ptr compare_val_expr;
            std::string tracking_var_;

            typedef std::shared_ptr<CompareAndSwapStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<CompareAndSwapStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct PrintStmt : public Stmt {
            Expr::Ptr expr;
            std::string format;

            typedef std::shared_ptr<PrintStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PrintStmt>());
            }


        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct IdentDecl : public MIRNode {
            std::string name;
            Type::Ptr type;

            typedef std::shared_ptr<IdentDecl> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<IdentDecl>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct VarDecl : public Stmt {
            std::string modifier;
            std::string name;
            Type::Ptr type;
            Expr::Ptr initVal;
            //field to keep track of whether the variable needs allocation. Used only for vectors
            bool needs_allocation = true;
            typedef std::shared_ptr<VarDecl> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VarDecl>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct StructTypeDecl : public Type {
            std::string name;
            std::vector<VarDecl::Ptr> fields;

            typedef std::shared_ptr<StructTypeDecl> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<StructTypeDecl>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct VarExpr : public Expr {
            mir::Var var;
            typedef std::shared_ptr<VarExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VarExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct FuncDecl : public MIRNode {
            enum class Type {
                INTERNAL, EXPORTED, EXTERNAL
            };
	    enum function_context_type {
		CONTEXT_NONE = 0x0,
		CONTEXT_HOST = 0x1,
		CONTEXT_DEVICE = 0x2,
		CONTEXT_BOTH = 0x3,
	    };

            std::string name;
            std::vector<mir::Var> args;
            mir::Var result;
            Type type;
            //TODO: replace this with a statement
            StmtBlock::Ptr body;
	

            typedef std::shared_ptr<FuncDecl> Ptr;

            FuncDecl() {
              std::unordered_map<std::string, FieldVectorProperty> field_vector_properties_map_;
              this->setMetadata<std::unordered_map<std::string, FieldVectorProperty>>("field_vector_properties_map_", field_vector_properties_map_);
              this->setMetadata<function_context_type>("function_context", function_context_type::CONTEXT_HOST);
            }
            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<FuncDecl>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };
	static inline FuncDecl::function_context_type operator | (FuncDecl::function_context_type a, FuncDecl::function_context_type b) {
		return static_cast<FuncDecl::function_context_type>((int)a | (int)b);
	}
	static inline FuncDecl::function_context_type operator & (FuncDecl::function_context_type a, FuncDecl::function_context_type b) {
		return static_cast<FuncDecl::function_context_type>((int)a & (int)b);
	}
	static inline FuncDecl::function_context_type& operator |= (FuncDecl::function_context_type &a, FuncDecl::function_context_type b) {
		a = a | b;
		return a;
	}
	

        struct TensorReadExpr : public Expr {
            Expr::Ptr index;
            Expr::Ptr target;

            //convenience constructor for building a tensor read expr using code
            TensorReadExpr(std::string input_target,
                           std::string input_index,
                           mir::Type::Ptr target_type,
                           mir::Type::Ptr index_type) {
                mir::VarExpr::Ptr target_expr = std::make_shared<mir::VarExpr>();
                mir::Var target_var = mir::Var(input_target, target_type);
                target_expr->var = target_var;
                target = target_expr;
                mir::VarExpr::Ptr index_expr = std::make_shared<mir::VarExpr>();
                mir::Var index_var = mir::Var(input_index, index_type);
                index_expr->var = index_var;
                index = index_expr;
            }

            std::string getTargetNameStr() {
                //check for tensorarray expression and then return empty string
                std::string empty = "";
                if(mir::isa<mir::TensorReadExpr>(target.get()->shared_from_this())) {
                    return empty;}


                auto target_expr = mir::to<mir::VarExpr>(target);
                auto target_name = target_expr->var.getName();
                return target_name;
            }

            std::string getIndexNameStr() {
                auto index_expr = mir::to<mir::VarExpr>(index);
                auto index_name = index_expr->var.getName();
                return index_name;
            }

            TensorReadExpr() {}

            typedef std::shared_ptr<TensorReadExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<TensorReadExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct TensorStructReadExpr : TensorReadExpr {
            typedef std::shared_ptr<TensorStructReadExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<TensorStructReadExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct TensorArrayReadExpr : public TensorReadExpr {

            typedef std::shared_ptr<TensorArrayReadExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<TensorArrayReadExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        /// Calls a function that may any number of arguments.
        struct Call : public Expr {
            std::string name;
            std::vector<Expr::Ptr> args;
            Type::Ptr generic_type;
            typedef std::shared_ptr<Call> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<Call>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        // this can be either a PriorityUpdateSum or PriorityUpdateMin
        struct PriorityUpdateOperator : public Call {
            Expr::Ptr priority_queue;
            //the node whose priority is going to be updated
            Expr::Ptr destination_node_id;
            std::string tracking_var;

            PriorityUpdateOperator() {
              this->setMetadata<bool>("is_atomic", false);
            }

            typedef std::shared_ptr<PriorityUpdateOperator> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PriorityUpdateOperator>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();


        };


        struct PriorityUpdateOperatorMin : public PriorityUpdateOperator {
            Expr::Ptr new_val;
            //old priority value (could be optional)
            Expr::Ptr old_val;

            typedef std::shared_ptr<PriorityUpdateOperatorMin> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PriorityUpdateOperatorMin>());
            }


        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();


        };


        struct PriorityUpdateOperatorSum : public PriorityUpdateOperator {
            typedef std::shared_ptr<PriorityUpdateOperatorSum> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PriorityUpdateOperatorSum>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();


        };



        struct LoadExpr : public Expr {
            Expr::Ptr file_name;
            typedef std::shared_ptr<LoadExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<LoadExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };
        

        
        struct EdgeSetLoadExpr : public Expr {
            Expr::Ptr file_name;
            bool is_weighted_ = false;
            typedef std::shared_ptr<EdgeSetLoadExpr> Ptr;
     

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<EdgeSetLoadExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct ApplyExpr : public Expr {
            Expr::Ptr target;
            std::string input_function_name = "";
            std::string tracking_field = "";
            typedef std::shared_ptr<ApplyExpr> Ptr;

	    std::string kernel_function;
	    ApplyExpr() {
	      this->setMetadata<bool>("requires_output", false);
	    }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct VertexSetApplyExpr : public ApplyExpr {
            typedef std::shared_ptr<VertexSetApplyExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VertexSetApplyExpr>());
            }

            VertexSetApplyExpr() {
              this->setMetadata<bool>("is_parallel", true);
            }

            VertexSetApplyExpr(std::string target_name,
                               mir::Type::Ptr target_type,
                               std::string function_name) {
                mir::VarExpr::Ptr target_expr = std::make_shared<mir::VarExpr>();
                mir::Var target_var = mir::Var(target_name, target_type);
                target_expr->var = target_var;
                target = target_expr;
                input_function_name = function_name;
                this->setMetadata<bool>("is_parallel", true);
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct MergeReduceField {
            std::string field_name;
            ScalarType::Ptr scalar_type;
            ReduceStmt::ReductionOp reduce_op;
            bool numa_aware;

            typedef std::shared_ptr<MergeReduceField> Ptr;
        };

        struct EdgeSetApplyExpr : public ApplyExpr {
            std::string from_func = "";
            std::string to_func = "";
            bool is_weighted = false;

            EdgeSetApplyExpr() {
              this->setMetadata<bool>("enable_deduplication", false);
              this->setMetadata<bool>("is_parallel", false);
            }

            typedef std::shared_ptr<EdgeSetApplyExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<EdgeSetApplyExpr>());
            }

            void copyEdgesetApply(MIRNode::Ptr node){
                copy(node);
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct PushEdgeSetApplyExpr : EdgeSetApplyExpr {
            typedef std::shared_ptr<PushEdgeSetApplyExpr> Ptr;

            PushEdgeSetApplyExpr() {}

            PushEdgeSetApplyExpr(EdgeSetApplyExpr::Ptr edgeset_apply) {
                target = edgeset_apply->target;
                input_function_name = edgeset_apply->input_function_name;
                from_func = edgeset_apply->from_func;
                to_func = edgeset_apply->to_func;
                tracking_field = edgeset_apply->tracking_field;
                is_weighted = edgeset_apply->is_weighted;
                metadata_map = edgeset_apply->metadata_map;
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PushEdgeSetApplyExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct PullEdgeSetApplyExpr : EdgeSetApplyExpr {
            typedef std::shared_ptr<PullEdgeSetApplyExpr> Ptr;

            PullEdgeSetApplyExpr() {}

            PullEdgeSetApplyExpr(EdgeSetApplyExpr::Ptr edgeset_apply) {
                target = edgeset_apply->target;
                input_function_name = edgeset_apply->input_function_name;
                from_func = edgeset_apply->from_func;
                to_func = edgeset_apply->to_func;
                tracking_field = edgeset_apply->tracking_field;
                is_weighted = edgeset_apply->is_weighted;
                metadata_map = edgeset_apply->metadata_map;
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PullEdgeSetApplyExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct HybridDenseForwardEdgeSetApplyExpr : EdgeSetApplyExpr {
            typedef std::shared_ptr<HybridDenseForwardEdgeSetApplyExpr> Ptr;

            HybridDenseForwardEdgeSetApplyExpr() {}


            HybridDenseForwardEdgeSetApplyExpr(EdgeSetApplyExpr::Ptr edgeset_apply) {
                target = edgeset_apply->target;
                // for hybrid dense  forward, it is always using the push function (atomics on dst)
                // it is ok with just one direction
                input_function_name = edgeset_apply->input_function_name;
                from_func = edgeset_apply->from_func;
                to_func = edgeset_apply->to_func;
                tracking_field = edgeset_apply->tracking_field;
                is_weighted = edgeset_apply->is_weighted;
                metadata_map = edgeset_apply->metadata_map;
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<HybridDenseForwardEdgeSetApplyExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct HybridDenseEdgeSetApplyExpr : EdgeSetApplyExpr {
            typedef std::shared_ptr<HybridDenseEdgeSetApplyExpr> Ptr;

            HybridDenseEdgeSetApplyExpr() {}

            HybridDenseEdgeSetApplyExpr(EdgeSetApplyExpr::Ptr edgeset_apply) {
                target = edgeset_apply->target;
                // for hybrid dense  forward, it is always using the push function (atomics on dst)
                // it is ok with just one direction
                input_function_name = edgeset_apply->input_function_name;
                from_func = edgeset_apply->from_func;
                to_func = edgeset_apply->to_func;
                tracking_field = edgeset_apply->tracking_field;
                is_weighted = edgeset_apply->is_weighted;
                metadata_map = edgeset_apply->metadata_map;
                this->setMetadata<std::string>("push_to_function_", edgeset_apply->to_func);
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<HybridDenseEdgeSetApplyExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct WhereExpr : public Expr {
            std::string target;
            bool is_constant_set = false;
            std::string input_func;
            typedef std::shared_ptr<WhereExpr> Ptr;

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct VertexSetWhereExpr : public WhereExpr {
            typedef std::shared_ptr<VertexSetWhereExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VertexSetWhereExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct EdgeSetWhereExpr : public WhereExpr {
            typedef std::shared_ptr<EdgeSetWhereExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<EdgeSetWhereExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct NewExpr : public Expr {
            typedef std::shared_ptr<NewExpr> Ptr;
            ElementType::Ptr element_type;
        };

        struct VertexSetAllocExpr : public NewExpr {
            Expr::Ptr size_expr;
            enum class Layout {
                SPARSE,
                DENSE
            };
            typedef std::shared_ptr<VertexSetAllocExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VertexSetAllocExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct VectorAllocExpr : public NewExpr {
            Expr::Ptr size_expr;
            ScalarType::Ptr scalar_type;
            // the element of the vector can potentially be a vector
            // vector{Vertex}(vector[20])
            VectorType::Ptr vector_type;

            typedef std::shared_ptr<VectorAllocExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<VectorAllocExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };


        struct ListAllocExpr : public NewExpr {
            Expr::Ptr size_expr;
            Type::Ptr element_type;
            typedef std::shared_ptr<ListAllocExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<ListAllocExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct NaryExpr : public Expr {
            std::vector<Expr::Ptr> operands;
            typedef std::shared_ptr<NaryExpr> Ptr;

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct BinaryExpr : public Expr {
            Expr::Ptr lhs, rhs;
            typedef std::shared_ptr<BinaryExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<BinaryExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct NegExpr : public Expr {
            bool negate = false;
            Expr::Ptr operand;

            typedef std::shared_ptr<NegExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<NegExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct EqExpr : public NaryExpr {
            enum class Op {
                LT, LE, GT, GE, EQ, NE
            };

            std::vector<Op> ops;

            typedef std::shared_ptr<EqExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<EqExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

	struct AndExpr : public BinaryExpr {
            typedef std::shared_ptr<AndExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<AndExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct OrExpr : public BinaryExpr {
            typedef std::shared_ptr<OrExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<OrExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct XorExpr : public BinaryExpr {
            typedef std::shared_ptr<XorExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<XorExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct NotExpr : public Expr {
            Expr::Ptr operand;

            typedef std::shared_ptr<NotExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<NotExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct AddExpr : public BinaryExpr {
            typedef std::shared_ptr<AddExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<AddExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct MulExpr : public BinaryExpr {
            typedef std::shared_ptr<MulExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<MulExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct DivExpr : public BinaryExpr {
            typedef std::shared_ptr<DivExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<DivExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct SubExpr : public BinaryExpr {
            typedef std::shared_ptr<SubExpr> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<SubExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct IfStmt : public Stmt {
            Expr::Ptr cond;
            Stmt::Ptr ifBody;
            Stmt::Ptr elseBody;

            typedef std::shared_ptr<IfStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<IfStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct BreakStmt : public Stmt {
            typedef std::shared_ptr<BreakStmt> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<BreakStmt>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


        struct PriorityQueueType : public Type {
            ElementType::Ptr element;
            ScalarType::Ptr priority_type;

            typedef std::shared_ptr<PriorityQueueType> Ptr;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PriorityQueueType>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct PriorityQueueAllocExpr : public NewExpr {
            typedef std::shared_ptr<PriorityQueueAllocExpr> Ptr;

            bool dup_within_bucket;
            bool dup_across_bucket;
            std::string vector_function;
            int bucket_ordering;
            int priority_ordering;
            bool init_bucket;
            Expr::Ptr starting_node;

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<PriorityQueueAllocExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();

        };

        struct UpdatePriorityEdgeSetApplyExpr : EdgeSetApplyExpr {
            typedef std::shared_ptr<UpdatePriorityEdgeSetApplyExpr> Ptr;

            UpdatePriorityEdgeSetApplyExpr() {}

            UpdatePriorityEdgeSetApplyExpr(EdgeSetApplyExpr::Ptr edgeset_apply) {
                target = edgeset_apply->target;
                input_function_name = edgeset_apply->input_function_name;
                from_func = edgeset_apply->from_func;
                to_func = edgeset_apply->to_func;
                tracking_field = edgeset_apply->tracking_field;
                is_weighted = edgeset_apply->is_weighted;
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<UpdatePriorityEdgeSetApplyExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);
            virtual MIRNode::Ptr cloneNode();
        };

        struct UpdatePriorityExternVertexSetApplyExpr : VertexSetApplyExpr {
            typedef std::shared_ptr<UpdatePriorityExternVertexSetApplyExpr> Ptr;

            UpdatePriorityExternVertexSetApplyExpr() {}

            UpdatePriorityExternVertexSetApplyExpr(VertexSetApplyExpr::Ptr vertexset_apply) {
                target = vertexset_apply->target;
                input_function_name = vertexset_apply->input_function_name;
                tracking_field = vertexset_apply->tracking_field;
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<UpdatePriorityExternVertexSetApplyExpr>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct UpdatePriorityUpdateBucketsCall : Stmt {
            std::string priority_queue_name;
            std::string lambda_name;
            std::string modified_vertexsubset_name;
            bool nodes_init_in_bucket;

            typedef std::shared_ptr<UpdatePriorityUpdateBucketsCall> Ptr;

            UpdatePriorityUpdateBucketsCall() {
              this->setMetadata<int>("delta", 1);
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<UpdatePriorityUpdateBucketsCall>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };

        struct UpdatePriorityExternCall : Stmt {
            Expr::Ptr input_set;
            std::string priority_queue_name;
            std::string output_set_name;
            std::string lambda_name;
            std::string apply_function_name;

            typedef std::shared_ptr<UpdatePriorityExternCall> Ptr;

            UpdatePriorityExternCall() {}

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<UpdatePriorityExternCall>());
            }

        protected:
            virtual void copy(MIRNode::Ptr);

            virtual MIRNode::Ptr cloneNode();
        };


	struct UpdatePriorityEdgeCountEdgeSetApplyExpr: public UpdatePriorityEdgeSetApplyExpr {
		std::string lambda_name;
		std::string moved_object_name;
		
		std::string priority_queue_name;
	
		typedef std::shared_ptr<UpdatePriorityEdgeCountEdgeSetApplyExpr> Ptr;
		UpdatePriorityEdgeCountEdgeSetApplyExpr() {}
		
		virtual void accept(MIRVisitor *visitor) {
			visitor->visit(self<UpdatePriorityEdgeCountEdgeSetApplyExpr>());
		}
                void copyFrom(UpdatePriorityEdgeSetApplyExpr::Ptr op) {
			UpdatePriorityEdgeSetApplyExpr::copy(op);
		}
		protected:	
		virtual void copy(MIRNode::Ptr);
		virtual MIRNode::Ptr cloneNode();
	};

        struct OrderedProcessingOperator : Stmt {
            Expr::Ptr while_cond_expr;
            std::string edge_update_func;
            std::string priority_queue_name;
            Expr::Ptr optional_source_node;
            Expr::Ptr graph_name;

            typedef std::shared_ptr<OrderedProcessingOperator> Ptr;

            OrderedProcessingOperator() {
              //the threshold used for merging buckets
              this->setMetadata<int>("merge_threshold", 0);
            }

            virtual void accept(MIRVisitor *visitor) {
                visitor->visit(self<OrderedProcessingOperator>());
            }

            protected:
                virtual void copy(MIRNode::Ptr);

                virtual MIRNode::Ptr cloneNode();

        };


	// GPU Specific operators
	struct VertexSetDedupExpr: Expr {
		Expr::Ptr target;
		typedef std::shared_ptr<VertexSetDedupExpr> Ptr;
		virtual void accept(MIRVisitor *visitor) {
			visitor->visit(self<VertexSetDedupExpr>());
		}
		protected:
		virtual void copy(MIRNode::Ptr);
		virtual MIRNode::Ptr cloneNode();		
	};
	struct HybridGPUStmt: Stmt {
		StmtBlock::Ptr stmt1;
		StmtBlock::Ptr stmt2;
		std::string threshold_var_name;

		typedef std::shared_ptr<HybridGPUStmt> Ptr;
		virtual void accept(MIRVisitor *visitor) {
			visitor->visit(self<HybridGPUStmt>());
		}
	protected:
		virtual void copy(MIRNode::Ptr);
		virtual MIRNode::Ptr cloneNode();
	};
	struct EnqueueVertex: Stmt {
		Expr::Ptr vertex_id;
		Expr::Ptr vertex_frontier;

		enum class Type {SPARSE, BOOLMAP, BITMAP};

		typedef std::shared_ptr<EnqueueVertex> Ptr;
		virtual void accept(MIRVisitor *visitor) {
			visitor->visit(self<EnqueueVertex>());
		}
	protected:
		virtual void copy(MIRNode::Ptr);
		virtual MIRNode::Ptr cloneNode();
	};
    }

}

#endif //GRAPHIT_MIR_H
