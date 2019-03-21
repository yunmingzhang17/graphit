//
// Created by Yunming Zhang on 3/20/19.
//

#ifndef GRAPHIT_PRIORITY_FEATURES_LOWERING_H
#define GRAPHIT_PRIORITY_FEATURES_LOWERING_H

#include <graphit/midend/mir_context.h>
#include <graphit/frontend/schedule.h>
#include <graphit/midend/mir_rewriter.h>


namespace graphit {

    class PriorityFeaturesLower {
    public:

        PriorityFeaturesLower(MIRContext *mir_context) : mir_context_(mir_context) {};

        PriorityFeaturesLower(MIRContext *mir_context, Schedule *schedule)
                : schedule_(schedule), mir_context_(mir_context) {};


        struct PriorityUpdateScheduleFinder : public mir::MIRVisitor {
            using mir::MIRVisitor::visit;

            PriorityUpdateScheduleFinder(MIRContext* mir_context,  Schedule* schedule)
                : mir_context_(mir_context), schedule_(schedule){

            };

            void visit(mir::UpdatePriorityEdgeSetApplyExpr::Ptr update_priority_edgeset_apply_expr);

            void visit(mir::UpdatePriorityExternVertexSetApplyExpr::Ptr update_priority_extern_vertexset_apply_expr);

            void setPrioritySchedule(std::string current_label);

            Schedule *schedule_ = nullptr;
            MIRContext *mir_context_ = nullptr;

         };


        // use the schedule to set the type for MIR::PriorityQeueuType and PriorityQueueAllocExpr
        struct LowerPriorityQueueTypeandAllocExpr : public mir::MIRVisitor {
            using mir::MIRVisitor::visit;

            LowerPriorityQueueTypeandAllocExpr(Schedule* schedule) : schedule_(schedule){

            };

                void visit(mir::PriorityQueueType::Ptr){

            }

                void visit(mir::PriorityQueueAllocExpr::Ptr){

            }

            Schedule * schedule_;

        };

        struct LowerUpdatePriorityExternVertexSetApplyExpr : public mir::MIRRewriter {
	    using mir::MIRRewriter::visit;
	    LowerUpdatePriorityExternVertexSetApplyExpr(Schedule *schedule, MIRContext *mir_context) : schedule_(schedule), mir_context_(mir_context){
    
	    }
            virtual void visit(mir::ExprStmt::Ptr);
            Schedule * schedule_;
	    MIRContext *mir_context_;
	};


        void lower();

        //sets the priority update schedule
        void setCurrentPriorityUpdateSchedule();

    private:
        Schedule *schedule_ = nullptr;
        MIRContext *mir_context_ = nullptr;

    };
}


#endif //GRAPHIT_PRIORITY_FEATURES_LOWERING_H
