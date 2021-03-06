#include "duckdb/execution/aggregate_hashtable.hpp"
#include "duckdb/execution/operator/join/physical_delim_join.hpp"
#include "duckdb/execution/operator/join/physical_hash_join.hpp"
#include "duckdb/execution/operator/projection/physical_projection.hpp"
#include "duckdb/execution/operator/scan/physical_chunk_scan.hpp"
#include "duckdb/execution/physical_plan_generator.hpp"
#include "duckdb/planner/operator/logical_delim_join.hpp"
#include "duckdb/planner/expression/bound_aggregate_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/execution/operator/aggregate/physical_hash_aggregate.hpp"

using namespace std;

namespace duckdb {

static void GatherDelimScans(PhysicalOperator *op, vector<PhysicalOperator *> &delim_scans) {
	assert(op);
	if (op->type == PhysicalOperatorType::DELIM_SCAN) {
		delim_scans.push_back(op);
	}
	for (auto &child : op->children) {
		GatherDelimScans(child.get(), delim_scans);
	}
}

unique_ptr<PhysicalOperator> PhysicalPlanGenerator::CreatePlan(LogicalDelimJoin &op) {
	// first create the underlying join
	auto plan = CreatePlan((LogicalComparisonJoin &)op);
	// this should create a join, not a cross product
	assert(plan && plan->type != PhysicalOperatorType::CROSS_PRODUCT);
	// duplicate eliminated join
	// first gather the scans on the duplicate eliminated data set from the RHS
	vector<PhysicalOperator *> delim_scans;
	GatherDelimScans(plan->children[1].get(), delim_scans);
	if (delim_scans.size() == 0) {
		// no duplicate eliminated scans in the RHS!
		// in this case we don't need to create a delim join
		// just push the normal join
		return plan;
	}
	vector<TypeId> delim_types;
	vector<unique_ptr<Expression>> distinct_groups, distinct_expressions;
	for (auto &delim_expr : op.duplicate_eliminated_columns) {
		assert(delim_expr->type == ExpressionType::BOUND_REF);
		auto &bound_ref = (BoundReferenceExpression &)*delim_expr;
		delim_types.push_back(bound_ref.return_type);
		distinct_groups.push_back(make_unique<BoundReferenceExpression>(bound_ref.return_type, bound_ref.index));
	}
	if (op.join_type == JoinType::MARK) {
		assert(plan->type == PhysicalOperatorType::HASH_JOIN);
		auto &hash_join = (PhysicalHashJoin &)*plan;
		hash_join.delim_types = delim_types;
	}
	// now create the duplicate eliminated join
	auto delim_join = make_unique<PhysicalDelimJoin>(op.types, move(plan), delim_scans);
	// we still have to create the DISTINCT clause that is used to generate the duplicate eliminated chunk
	delim_join->distinct = make_unique<PhysicalHashAggregate>(delim_types, move(distinct_expressions),
	                                                          move(distinct_groups), PhysicalOperatorType::DISTINCT);
	return move(delim_join);
}

}
