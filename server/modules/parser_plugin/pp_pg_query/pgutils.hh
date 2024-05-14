/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "pp_pg_query.hh"

namespace pgutils
{

bool is_truthy(const A_Const& a_const);

#define PGU_TYPE_TAG_ENTRY(T) static NodeTag of(const T*) { return T_ ## T; }

// The content of the TypeTag class is taken from enum NodeTag in
// libpg_query/src/postgres/include/nodes/nodes.h.
//
// If something is not found, updating may be needed.

class TypeTag
{
public:
#if defined(EXECNODES_H)
    /*
     * TAGS FOR EXECUTOR NODES (execnodes.h)
     */
    PGU_TYPE_TAG_ENTRY(IndexInfo);
    PGU_TYPE_TAG_ENTRY(ExprContext);
    PGU_TYPE_TAG_ENTRY(ProjectionInfo);
    PGU_TYPE_TAG_ENTRY(JunkFilter);
    PGU_TYPE_TAG_ENTRY(OnConflictSetState);
    PGU_TYPE_TAG_ENTRY(MergeActionState);
    PGU_TYPE_TAG_ENTRY(ResultRelInfo);
    PGU_TYPE_TAG_ENTRY(EState);
    PGU_TYPE_TAG_ENTRY(TupleTableSlot);
#endif

#if defined(PLANNODES_H)
    /*
     * TAGS FOR PLAN NODES (plannodes.h)
     */
    PGU_TYPE_TAG_ENTRY(Plan);
    PGU_TYPE_TAG_ENTRY(Result);
    PGU_TYPE_TAG_ENTRY(ProjectSet);
    PGU_TYPE_TAG_ENTRY(ModifyTable);
    PGU_TYPE_TAG_ENTRY(Append);
    PGU_TYPE_TAG_ENTRY(MergeAppend);
    PGU_TYPE_TAG_ENTRY(RecursiveUnion);
    PGU_TYPE_TAG_ENTRY(BitmapAnd);
    PGU_TYPE_TAG_ENTRY(BitmapOr);
    PGU_TYPE_TAG_ENTRY(Scan);
    PGU_TYPE_TAG_ENTRY(SeqScan);
    PGU_TYPE_TAG_ENTRY(SampleScan);
    PGU_TYPE_TAG_ENTRY(IndexScan);
    PGU_TYPE_TAG_ENTRY(IndexOnlyScan);
    PGU_TYPE_TAG_ENTRY(BitmapIndexScan);
    PGU_TYPE_TAG_ENTRY(BitmapHeapScan);
    PGU_TYPE_TAG_ENTRY(TidScan);
    PGU_TYPE_TAG_ENTRY(TidRangeScan);
    PGU_TYPE_TAG_ENTRY(SubqueryScan);
    PGU_TYPE_TAG_ENTRY(FunctionScan);
    PGU_TYPE_TAG_ENTRY(ValuesScan);
    PGU_TYPE_TAG_ENTRY(TableFuncScan);
    PGU_TYPE_TAG_ENTRY(CteScan);
    PGU_TYPE_TAG_ENTRY(NamedTuplestoreScan);
    PGU_TYPE_TAG_ENTRY(WorkTableScan);
    PGU_TYPE_TAG_ENTRY(ForeignScan);
    PGU_TYPE_TAG_ENTRY(CustomScan);
    PGU_TYPE_TAG_ENTRY(Join);
    PGU_TYPE_TAG_ENTRY(NestLoop);
    PGU_TYPE_TAG_ENTRY(MergeJoin);
    PGU_TYPE_TAG_ENTRY(HashJoin);
    PGU_TYPE_TAG_ENTRY(Material);
    PGU_TYPE_TAG_ENTRY(Memoize);
    PGU_TYPE_TAG_ENTRY(Sort);
    PGU_TYPE_TAG_ENTRY(IncrementalSort);
    PGU_TYPE_TAG_ENTRY(Group);
    PGU_TYPE_TAG_ENTRY(Agg);
    PGU_TYPE_TAG_ENTRY(WindowAgg);
    PGU_TYPE_TAG_ENTRY(Unique);
    PGU_TYPE_TAG_ENTRY(Gather);
    PGU_TYPE_TAG_ENTRY(GatherMerge);
    PGU_TYPE_TAG_ENTRY(Hash);
    PGU_TYPE_TAG_ENTRY(SetOp);
    PGU_TYPE_TAG_ENTRY(LockRows);
    PGU_TYPE_TAG_ENTRY(Limit);
    /* these aren't subclasses of Plan: */
    PGU_TYPE_TAG_ENTRY(NestLoopParam);
    PGU_TYPE_TAG_ENTRY(PlanRowMark);
    PGU_TYPE_TAG_ENTRY(PartitionPruneInfo);
    PGU_TYPE_TAG_ENTRY(PartitionedRelPruneInfo);
    PGU_TYPE_TAG_ENTRY(PartitionPruneStepOp);
    PGU_TYPE_TAG_ENTRY(PartitionPruneStepCombine);
    PGU_TYPE_TAG_ENTRY(PlanInvalItem);
#endif

#if defined(EXECNODES_H)
    /*
     * TAGS FOR PLAN STATE NODES (execnodes.h)
     *
     * These should correspond one-to-one with Plan node types.
     */
    PGU_TYPE_TAG_ENTRY(PlanState);
    PGU_TYPE_TAG_ENTRY(ResultState);
    PGU_TYPE_TAG_ENTRY(ProjectSetState);
    PGU_TYPE_TAG_ENTRY(ModifyTableState);
    PGU_TYPE_TAG_ENTRY(AppendState);
    PGU_TYPE_TAG_ENTRY(MergeAppendState);
    PGU_TYPE_TAG_ENTRY(RecursiveUnionState);
    PGU_TYPE_TAG_ENTRY(BitmapAndState);
    PGU_TYPE_TAG_ENTRY(BitmapOrState);
    PGU_TYPE_TAG_ENTRY(ScanState);
    PGU_TYPE_TAG_ENTRY(SeqScanState);
    PGU_TYPE_TAG_ENTRY(SampleScanState);
    PGU_TYPE_TAG_ENTRY(IndexScanState);
    PGU_TYPE_TAG_ENTRY(IndexOnlyScanState);
    PGU_TYPE_TAG_ENTRY(BitmapIndexScanState);
    PGU_TYPE_TAG_ENTRY(BitmapHeapScanState);
    PGU_TYPE_TAG_ENTRY(TidScanState);
    PGU_TYPE_TAG_ENTRY(TidRangeScanState);
    PGU_TYPE_TAG_ENTRY(SubqueryScanState);
    PGU_TYPE_TAG_ENTRY(FunctionScanState);
    PGU_TYPE_TAG_ENTRY(TableFuncScanState);
    PGU_TYPE_TAG_ENTRY(ValuesScanState);
    PGU_TYPE_TAG_ENTRY(CteScanState);
    PGU_TYPE_TAG_ENTRY(NamedTuplestoreScanState);
    PGU_TYPE_TAG_ENTRY(WorkTableScanState);
    PGU_TYPE_TAG_ENTRY(ForeignScanState);
    PGU_TYPE_TAG_ENTRY(CustomScanState);
    PGU_TYPE_TAG_ENTRY(JoinState);
    PGU_TYPE_TAG_ENTRY(NestLoopState);
    PGU_TYPE_TAG_ENTRY(MergeJoinState);
    PGU_TYPE_TAG_ENTRY(HashJoinState);
    PGU_TYPE_TAG_ENTRY(MaterialState);
    PGU_TYPE_TAG_ENTRY(MemoizeState);
    PGU_TYPE_TAG_ENTRY(SortState);
    PGU_TYPE_TAG_ENTRY(IncrementalSortState);
    PGU_TYPE_TAG_ENTRY(GroupState);
    PGU_TYPE_TAG_ENTRY(AggState);
    PGU_TYPE_TAG_ENTRY(WindowAggState);
    PGU_TYPE_TAG_ENTRY(UniqueState);
    PGU_TYPE_TAG_ENTRY(GatherState);
    PGU_TYPE_TAG_ENTRY(GatherMergeState);
    PGU_TYPE_TAG_ENTRY(HashState);
    PGU_TYPE_TAG_ENTRY(SetOpState);
    PGU_TYPE_TAG_ENTRY(LockRowsState);
    PGU_TYPE_TAG_ENTRY(LimitState);
#endif

#if defined(PRIMNODES_H)
    /*
     * TAGS FOR PRIMITIVE NODES (primnodes.h)
     */
    PGU_TYPE_TAG_ENTRY(Alias);
    PGU_TYPE_TAG_ENTRY(RangeVar);
    PGU_TYPE_TAG_ENTRY(TableFunc);
    PGU_TYPE_TAG_ENTRY(Var);
    PGU_TYPE_TAG_ENTRY(Const);
    PGU_TYPE_TAG_ENTRY(Param);
    PGU_TYPE_TAG_ENTRY(Aggref);
    PGU_TYPE_TAG_ENTRY(GroupingFunc);
    PGU_TYPE_TAG_ENTRY(WindowFunc);
    PGU_TYPE_TAG_ENTRY(SubscriptingRef);
    PGU_TYPE_TAG_ENTRY(FuncExpr);
    PGU_TYPE_TAG_ENTRY(NamedArgExpr);
    PGU_TYPE_TAG_ENTRY(OpExpr);
    //PGU_TYPE_TAG_ENTRY(DistinctExpr); typedef OpExpr DistinctExpr;
    //PGU_TYPE_TAG_ENTRY(NullIfExpr);   typedef OpExpr NullIfExpr;
    PGU_TYPE_TAG_ENTRY(ScalarArrayOpExpr);
    PGU_TYPE_TAG_ENTRY(BoolExpr);
    PGU_TYPE_TAG_ENTRY(SubLink);
    PGU_TYPE_TAG_ENTRY(SubPlan);
    PGU_TYPE_TAG_ENTRY(AlternativeSubPlan);
    PGU_TYPE_TAG_ENTRY(FieldSelect);
    PGU_TYPE_TAG_ENTRY(FieldStore);
    PGU_TYPE_TAG_ENTRY(RelabelType);
    PGU_TYPE_TAG_ENTRY(CoerceViaIO);
    PGU_TYPE_TAG_ENTRY(ArrayCoerceExpr);
    PGU_TYPE_TAG_ENTRY(ConvertRowtypeExpr);
    PGU_TYPE_TAG_ENTRY(CollateExpr);
    PGU_TYPE_TAG_ENTRY(CaseExpr);
    PGU_TYPE_TAG_ENTRY(CaseWhen);
    PGU_TYPE_TAG_ENTRY(CaseTestExpr);
    PGU_TYPE_TAG_ENTRY(ArrayExpr);
    PGU_TYPE_TAG_ENTRY(RowExpr);
    PGU_TYPE_TAG_ENTRY(RowCompareExpr);
    PGU_TYPE_TAG_ENTRY(CoalesceExpr);
    PGU_TYPE_TAG_ENTRY(MinMaxExpr);
    PGU_TYPE_TAG_ENTRY(SQLValueFunction);
    PGU_TYPE_TAG_ENTRY(XmlExpr);
    PGU_TYPE_TAG_ENTRY(NullTest);
    PGU_TYPE_TAG_ENTRY(BooleanTest);
    PGU_TYPE_TAG_ENTRY(CoerceToDomain);
    PGU_TYPE_TAG_ENTRY(CoerceToDomainValue);
    PGU_TYPE_TAG_ENTRY(SetToDefault);
    PGU_TYPE_TAG_ENTRY(CurrentOfExpr);
    PGU_TYPE_TAG_ENTRY(NextValueExpr);
    PGU_TYPE_TAG_ENTRY(InferenceElem);
    PGU_TYPE_TAG_ENTRY(TargetEntry);
    PGU_TYPE_TAG_ENTRY(RangeTblRef);
    PGU_TYPE_TAG_ENTRY(JoinExpr);
    PGU_TYPE_TAG_ENTRY(FromExpr);
    PGU_TYPE_TAG_ENTRY(OnConflictExpr);
    PGU_TYPE_TAG_ENTRY(IntoClause);
#endif

#if defined(EXECNODES_H)
    /*
     * TAGS FOR EXPRESSION STATE NODES (execnodes.h)
     *
     * ExprState represents the evaluation state for a whole expression tree.
     * Most Expr-based plan nodes do not have a corresponding expression state
     * node, they're fully handled within execExpr* - but sometimes the state
     * needs to be shared with other parts of the executor, as for example
     * with SubPlanState, which nodeSubplan.c has to modify.
     */
    PGU_TYPE_TAG_ENTRY(ExprState);
    PGU_TYPE_TAG_ENTRY(WindowFuncExprState);
    PGU_TYPE_TAG_ENTRY(SetExprState);
    PGU_TYPE_TAG_ENTRY(SubPlanState);
    PGU_TYPE_TAG_ENTRY(DomainConstraintState);
#endif

#if defined(PATHNODES_H)
    /*
     * TAGS FOR PLANNER NODES (pathnodes.h)
     */
    PGU_TYPE_TAG_ENTRY(PlannerInfo);
    PGU_TYPE_TAG_ENTRY(PlannerGlobal);
    PGU_TYPE_TAG_ENTRY(RelOptInfo);
    PGU_TYPE_TAG_ENTRY(IndexOptInfo);
    PGU_TYPE_TAG_ENTRY(ForeignKeyOptInfo);
    PGU_TYPE_TAG_ENTRY(ParamPathInfo);
    PGU_TYPE_TAG_ENTRY(Path);
    PGU_TYPE_TAG_ENTRY(IndexPath);
    PGU_TYPE_TAG_ENTRY(BitmapHeapPath);
    PGU_TYPE_TAG_ENTRY(BitmapAndPath);
    PGU_TYPE_TAG_ENTRY(BitmapOrPath);
    PGU_TYPE_TAG_ENTRY(TidPath);
    PGU_TYPE_TAG_ENTRY(TidRangePath);
    PGU_TYPE_TAG_ENTRY(SubqueryScanPath);
    PGU_TYPE_TAG_ENTRY(ForeignPath);
    PGU_TYPE_TAG_ENTRY(CustomPath);
    PGU_TYPE_TAG_ENTRY(NestPath);
    PGU_TYPE_TAG_ENTRY(MergePath);
    PGU_TYPE_TAG_ENTRY(HashPath);
    PGU_TYPE_TAG_ENTRY(AppendPath);
    PGU_TYPE_TAG_ENTRY(MergeAppendPath);
    PGU_TYPE_TAG_ENTRY(GroupResultPath);
    PGU_TYPE_TAG_ENTRY(MaterialPath);
    PGU_TYPE_TAG_ENTRY(MemoizePath);
    PGU_TYPE_TAG_ENTRY(UniquePath);
    PGU_TYPE_TAG_ENTRY(GatherPath);
    PGU_TYPE_TAG_ENTRY(GatherMergePath);
    PGU_TYPE_TAG_ENTRY(ProjectionPath);
    PGU_TYPE_TAG_ENTRY(ProjectSetPath);
    PGU_TYPE_TAG_ENTRY(SortPath);
    PGU_TYPE_TAG_ENTRY(IncrementalSortPath);
    PGU_TYPE_TAG_ENTRY(GroupPath);
    PGU_TYPE_TAG_ENTRY(UpperUniquePath);
    PGU_TYPE_TAG_ENTRY(AggPath);
    PGU_TYPE_TAG_ENTRY(GroupingSetsPath);
    PGU_TYPE_TAG_ENTRY(MinMaxAggPath);
    PGU_TYPE_TAG_ENTRY(WindowAggPath);
    PGU_TYPE_TAG_ENTRY(SetOpPath);
    PGU_TYPE_TAG_ENTRY(RecursiveUnionPath);
    PGU_TYPE_TAG_ENTRY(LockRowsPath);
    PGU_TYPE_TAG_ENTRY(ModifyTablePath);
    PGU_TYPE_TAG_ENTRY(LimitPath);
    /* these aren't subclasses of Path: */
    PGU_TYPE_TAG_ENTRY(EquivalenceClass);
    PGU_TYPE_TAG_ENTRY(EquivalenceMember);
    PGU_TYPE_TAG_ENTRY(PathKey);
    PGU_TYPE_TAG_ENTRY(PathKeyInfo);
    PGU_TYPE_TAG_ENTRY(PathTarget);
    PGU_TYPE_TAG_ENTRY(RestrictInfo);
    PGU_TYPE_TAG_ENTRY(IndexClause);
    PGU_TYPE_TAG_ENTRY(PlaceHolderVar);
    PGU_TYPE_TAG_ENTRY(SpecialJoinInfo);
    PGU_TYPE_TAG_ENTRY(AppendRelInfo);
    PGU_TYPE_TAG_ENTRY(RowIdentityVarInfo);
    PGU_TYPE_TAG_ENTRY(PlaceHolderInfo);
    PGU_TYPE_TAG_ENTRY(MinMaxAggInfo);
    PGU_TYPE_TAG_ENTRY(PlannerParamItem);
    PGU_TYPE_TAG_ENTRY(RollupData);
    PGU_TYPE_TAG_ENTRY(GroupingSetData);
    PGU_TYPE_TAG_ENTRY(StatisticExtInfo);
    PGU_TYPE_TAG_ENTRY(MergeAction);
#endif

#if defined(MEMNODES_H)
    /*
     * TAGS FOR MEMORY NODES (memnodes.h)
     */
    //PGU_TYPE_TAG_ENTRY(AllocSetContext);
    //PGU_TYPE_TAG_ENTRY(SlabContext);
    //PGU_TYPE_TAG_ENTRY(GenerationContext);
#endif

#if defined(VALUE_H)
    /*
     * TAGS FOR VALUE NODES (value.h)
     */
    PGU_TYPE_TAG_ENTRY(Integer);
    PGU_TYPE_TAG_ENTRY(Float);
    PGU_TYPE_TAG_ENTRY(Boolean);
    PGU_TYPE_TAG_ENTRY(String);
    PGU_TYPE_TAG_ENTRY(BitString);
#endif

#if defined(PG_LIST_H)
    /*
     * TAGS FOR LIST NODES (pg_list.h)
     */
    PGU_TYPE_TAG_ENTRY(List);
    //PGU_TYPE_TAG_ENTRY(IntList); typedef List IntList;
    //PGU_TYPE_TAG_ENTRY(OidList); typedef List OidList;
#endif

#if defined(EXTENSIBLE_H)
    /*
     * TAGS FOR EXTENSIBLE NODES (extensible.h)
     */
    PGU_TYPE_TAG_ENTRY(ExtensibleNode);
#endif

#if defined(PARSENODES_H)
    /*
     * TAGS FOR STATEMENT NODES (mostly in parsenodes.h)
     */
    PGU_TYPE_TAG_ENTRY(RawStmt);
    PGU_TYPE_TAG_ENTRY(Query);
#if defined(PLANNODES_H)
    PGU_TYPE_TAG_ENTRY(PlannedStmt);
#endif
    PGU_TYPE_TAG_ENTRY(InsertStmt);
    PGU_TYPE_TAG_ENTRY(DeleteStmt);
    PGU_TYPE_TAG_ENTRY(UpdateStmt);
    PGU_TYPE_TAG_ENTRY(MergeStmt);
    PGU_TYPE_TAG_ENTRY(SelectStmt);
    PGU_TYPE_TAG_ENTRY(ReturnStmt);
    PGU_TYPE_TAG_ENTRY(PLAssignStmt);
    PGU_TYPE_TAG_ENTRY(AlterTableStmt);
    PGU_TYPE_TAG_ENTRY(AlterTableCmd);
    PGU_TYPE_TAG_ENTRY(AlterDomainStmt);
    PGU_TYPE_TAG_ENTRY(SetOperationStmt);
    PGU_TYPE_TAG_ENTRY(GrantStmt);
    PGU_TYPE_TAG_ENTRY(GrantRoleStmt);
    PGU_TYPE_TAG_ENTRY(AlterDefaultPrivilegesStmt);
    PGU_TYPE_TAG_ENTRY(ClosePortalStmt);
    PGU_TYPE_TAG_ENTRY(ClusterStmt);
    PGU_TYPE_TAG_ENTRY(CopyStmt);
    PGU_TYPE_TAG_ENTRY(CreateStmt);
    PGU_TYPE_TAG_ENTRY(DefineStmt);
    PGU_TYPE_TAG_ENTRY(DropStmt);
    PGU_TYPE_TAG_ENTRY(TruncateStmt);
    PGU_TYPE_TAG_ENTRY(CommentStmt);
    PGU_TYPE_TAG_ENTRY(FetchStmt);
    PGU_TYPE_TAG_ENTRY(IndexStmt);
    PGU_TYPE_TAG_ENTRY(CreateFunctionStmt);
    PGU_TYPE_TAG_ENTRY(AlterFunctionStmt);
    PGU_TYPE_TAG_ENTRY(DoStmt);
    PGU_TYPE_TAG_ENTRY(RenameStmt);
    PGU_TYPE_TAG_ENTRY(RuleStmt);
    PGU_TYPE_TAG_ENTRY(NotifyStmt);
    PGU_TYPE_TAG_ENTRY(ListenStmt);
    PGU_TYPE_TAG_ENTRY(UnlistenStmt);
    PGU_TYPE_TAG_ENTRY(TransactionStmt);
    PGU_TYPE_TAG_ENTRY(ViewStmt);
    PGU_TYPE_TAG_ENTRY(LoadStmt);
    PGU_TYPE_TAG_ENTRY(CreateDomainStmt);
    PGU_TYPE_TAG_ENTRY(CreatedbStmt);
    PGU_TYPE_TAG_ENTRY(DropdbStmt);
    PGU_TYPE_TAG_ENTRY(VacuumStmt);
    PGU_TYPE_TAG_ENTRY(ExplainStmt);
    PGU_TYPE_TAG_ENTRY(CreateTableAsStmt);
    PGU_TYPE_TAG_ENTRY(CreateSeqStmt);
    PGU_TYPE_TAG_ENTRY(AlterSeqStmt);
    PGU_TYPE_TAG_ENTRY(VariableSetStmt);
    PGU_TYPE_TAG_ENTRY(VariableShowStmt);
    PGU_TYPE_TAG_ENTRY(DiscardStmt);
    PGU_TYPE_TAG_ENTRY(CreateTrigStmt);
    PGU_TYPE_TAG_ENTRY(CreatePLangStmt);
    PGU_TYPE_TAG_ENTRY(CreateRoleStmt);
    PGU_TYPE_TAG_ENTRY(AlterRoleStmt);
    PGU_TYPE_TAG_ENTRY(DropRoleStmt);
    PGU_TYPE_TAG_ENTRY(LockStmt);
    PGU_TYPE_TAG_ENTRY(ConstraintsSetStmt);
    PGU_TYPE_TAG_ENTRY(ReindexStmt);
    PGU_TYPE_TAG_ENTRY(CheckPointStmt);
    PGU_TYPE_TAG_ENTRY(CreateSchemaStmt);
    PGU_TYPE_TAG_ENTRY(AlterDatabaseStmt);
    PGU_TYPE_TAG_ENTRY(AlterDatabaseRefreshCollStmt);
    PGU_TYPE_TAG_ENTRY(AlterDatabaseSetStmt);
    PGU_TYPE_TAG_ENTRY(AlterRoleSetStmt);
    PGU_TYPE_TAG_ENTRY(CreateConversionStmt);
    PGU_TYPE_TAG_ENTRY(CreateCastStmt);
    PGU_TYPE_TAG_ENTRY(CreateOpClassStmt);
    PGU_TYPE_TAG_ENTRY(CreateOpFamilyStmt);
    PGU_TYPE_TAG_ENTRY(AlterOpFamilyStmt);
    PGU_TYPE_TAG_ENTRY(PrepareStmt);
    PGU_TYPE_TAG_ENTRY(ExecuteStmt);
    PGU_TYPE_TAG_ENTRY(DeallocateStmt);
    PGU_TYPE_TAG_ENTRY(DeclareCursorStmt);
    PGU_TYPE_TAG_ENTRY(CreateTableSpaceStmt);
    PGU_TYPE_TAG_ENTRY(DropTableSpaceStmt);
    PGU_TYPE_TAG_ENTRY(AlterObjectDependsStmt);
    PGU_TYPE_TAG_ENTRY(AlterObjectSchemaStmt);
    PGU_TYPE_TAG_ENTRY(AlterOwnerStmt);
    PGU_TYPE_TAG_ENTRY(AlterOperatorStmt);
    PGU_TYPE_TAG_ENTRY(AlterTypeStmt);
    PGU_TYPE_TAG_ENTRY(DropOwnedStmt);
    PGU_TYPE_TAG_ENTRY(ReassignOwnedStmt);
    PGU_TYPE_TAG_ENTRY(CompositeTypeStmt);
    PGU_TYPE_TAG_ENTRY(CreateEnumStmt);
    PGU_TYPE_TAG_ENTRY(CreateRangeStmt);
    PGU_TYPE_TAG_ENTRY(AlterEnumStmt);
    PGU_TYPE_TAG_ENTRY(AlterTSDictionaryStmt);
    PGU_TYPE_TAG_ENTRY(AlterTSConfigurationStmt);
    PGU_TYPE_TAG_ENTRY(CreateFdwStmt);
    PGU_TYPE_TAG_ENTRY(AlterFdwStmt);
    PGU_TYPE_TAG_ENTRY(CreateForeignServerStmt);
    PGU_TYPE_TAG_ENTRY(AlterForeignServerStmt);
    PGU_TYPE_TAG_ENTRY(CreateUserMappingStmt);
    PGU_TYPE_TAG_ENTRY(AlterUserMappingStmt);
    PGU_TYPE_TAG_ENTRY(DropUserMappingStmt);
    PGU_TYPE_TAG_ENTRY(AlterTableSpaceOptionsStmt);
    PGU_TYPE_TAG_ENTRY(AlterTableMoveAllStmt);
    PGU_TYPE_TAG_ENTRY(SecLabelStmt);
    PGU_TYPE_TAG_ENTRY(CreateForeignTableStmt);
    PGU_TYPE_TAG_ENTRY(ImportForeignSchemaStmt);
    PGU_TYPE_TAG_ENTRY(CreateExtensionStmt);
    PGU_TYPE_TAG_ENTRY(AlterExtensionStmt);
    PGU_TYPE_TAG_ENTRY(AlterExtensionContentsStmt);
    PGU_TYPE_TAG_ENTRY(CreateEventTrigStmt);
    PGU_TYPE_TAG_ENTRY(AlterEventTrigStmt);
    PGU_TYPE_TAG_ENTRY(RefreshMatViewStmt);
    PGU_TYPE_TAG_ENTRY(ReplicaIdentityStmt);
    PGU_TYPE_TAG_ENTRY(AlterSystemStmt);
    PGU_TYPE_TAG_ENTRY(CreatePolicyStmt);
    PGU_TYPE_TAG_ENTRY(AlterPolicyStmt);
    PGU_TYPE_TAG_ENTRY(CreateTransformStmt);
    PGU_TYPE_TAG_ENTRY(CreateAmStmt);
    PGU_TYPE_TAG_ENTRY(CreatePublicationStmt);
    PGU_TYPE_TAG_ENTRY(AlterPublicationStmt);
    PGU_TYPE_TAG_ENTRY(CreateSubscriptionStmt);
    PGU_TYPE_TAG_ENTRY(AlterSubscriptionStmt);
    PGU_TYPE_TAG_ENTRY(DropSubscriptionStmt);
    PGU_TYPE_TAG_ENTRY(CreateStatsStmt);
    PGU_TYPE_TAG_ENTRY(AlterCollationStmt);
    PGU_TYPE_TAG_ENTRY(CallStmt);
    PGU_TYPE_TAG_ENTRY(AlterStatsStmt);

    /*
     * TAGS FOR PARSE TREE NODES (parsenodes.h)
     */
    PGU_TYPE_TAG_ENTRY(A_Expr);
    PGU_TYPE_TAG_ENTRY(ColumnRef);
    PGU_TYPE_TAG_ENTRY(ParamRef);
    PGU_TYPE_TAG_ENTRY(A_Const);
    PGU_TYPE_TAG_ENTRY(FuncCall);
    PGU_TYPE_TAG_ENTRY(A_Star);
    PGU_TYPE_TAG_ENTRY(A_Indices);
    PGU_TYPE_TAG_ENTRY(A_Indirection);
    PGU_TYPE_TAG_ENTRY(A_ArrayExpr);
    PGU_TYPE_TAG_ENTRY(ResTarget);
    PGU_TYPE_TAG_ENTRY(MultiAssignRef);
    PGU_TYPE_TAG_ENTRY(TypeCast);
    PGU_TYPE_TAG_ENTRY(CollateClause);
    PGU_TYPE_TAG_ENTRY(SortBy);
    PGU_TYPE_TAG_ENTRY(WindowDef);
    PGU_TYPE_TAG_ENTRY(RangeSubselect);
    PGU_TYPE_TAG_ENTRY(RangeFunction);
    PGU_TYPE_TAG_ENTRY(RangeTableSample);
    PGU_TYPE_TAG_ENTRY(RangeTableFunc);
    PGU_TYPE_TAG_ENTRY(RangeTableFuncCol);
    PGU_TYPE_TAG_ENTRY(TypeName);
    PGU_TYPE_TAG_ENTRY(ColumnDef);
    PGU_TYPE_TAG_ENTRY(IndexElem);
    PGU_TYPE_TAG_ENTRY(StatsElem);
    PGU_TYPE_TAG_ENTRY(Constraint);
    PGU_TYPE_TAG_ENTRY(DefElem);
    PGU_TYPE_TAG_ENTRY(RangeTblEntry);
    PGU_TYPE_TAG_ENTRY(RangeTblFunction);
    PGU_TYPE_TAG_ENTRY(TableSampleClause);
    PGU_TYPE_TAG_ENTRY(WithCheckOption);
    PGU_TYPE_TAG_ENTRY(SortGroupClause);
    PGU_TYPE_TAG_ENTRY(GroupingSet);
    PGU_TYPE_TAG_ENTRY(WindowClause);
    PGU_TYPE_TAG_ENTRY(ObjectWithArgs);
    PGU_TYPE_TAG_ENTRY(AccessPriv);
    PGU_TYPE_TAG_ENTRY(CreateOpClassItem);
    PGU_TYPE_TAG_ENTRY(TableLikeClause);
    PGU_TYPE_TAG_ENTRY(FunctionParameter);
    PGU_TYPE_TAG_ENTRY(LockingClause);
    PGU_TYPE_TAG_ENTRY(RowMarkClause);
    PGU_TYPE_TAG_ENTRY(XmlSerialize);
    PGU_TYPE_TAG_ENTRY(WithClause);
    PGU_TYPE_TAG_ENTRY(InferClause);
    PGU_TYPE_TAG_ENTRY(OnConflictClause);
    PGU_TYPE_TAG_ENTRY(CTESearchClause);
    PGU_TYPE_TAG_ENTRY(CTECycleClause);
    PGU_TYPE_TAG_ENTRY(CommonTableExpr);
    PGU_TYPE_TAG_ENTRY(MergeWhenClause);
    PGU_TYPE_TAG_ENTRY(RoleSpec);
    PGU_TYPE_TAG_ENTRY(TriggerTransition);
    PGU_TYPE_TAG_ENTRY(PartitionElem);
    PGU_TYPE_TAG_ENTRY(PartitionSpec);
    PGU_TYPE_TAG_ENTRY(PartitionBoundSpec);
    PGU_TYPE_TAG_ENTRY(PartitionRangeDatum);
    PGU_TYPE_TAG_ENTRY(PartitionCmd);
    PGU_TYPE_TAG_ENTRY(VacuumRelation);
    PGU_TYPE_TAG_ENTRY(PublicationObjSpec);
    PGU_TYPE_TAG_ENTRY(PublicationTable);
#endif

#if defined(REPLNODES_H)
    /*
     * TAGS FOR REPLICATION GRAMMAR PARSE NODES (replnodes.h)
     */
    PGU_TYPE_TAG_ENTRY(IdentifySystemCmd);
    PGU_TYPE_TAG_ENTRY(BaseBackupCmd);
    PGU_TYPE_TAG_ENTRY(CreateReplicationSlotCmd);
    PGU_TYPE_TAG_ENTRY(DropReplicationSlotCmd);
    PGU_TYPE_TAG_ENTRY(ReadReplicationSlotCmd);
    PGU_TYPE_TAG_ENTRY(StartReplicationCmd);
    PGU_TYPE_TAG_ENTRY(TimeLineHistoryCmd);
#endif

#if defined(PGU_NEEDS_RANDOM_OTHER_STUFF)
    /*
     * TAGS FOR RANDOM OTHER STUFF
     *
     * These are objects that aren't part of parse/plan/execute node tree
     * structures, but we give them NodeTags anyway for identification
     * purposes (usually because they are involved in APIs where we want to
     * pass multiple object types through the same pointer).
     */
    PGU_TYPE_TAG_ENTRY(TriggerData);				/* in commands/trigger.h */
    PGU_TYPE_TAG_ENTRY(EventTriggerData);			/* in commands/event_trigger.h */
    PGU_TYPE_TAG_ENTRY(ReturnSetInfo);			/* in nodes/execnodes.h */
    PGU_TYPE_TAG_ENTRY(WindowObjectData);			/* private in nodeWindowAgg.c */
    PGU_TYPE_TAG_ENTRY(TIDBitmap);				/* in nodes/tidbitmap.h */
    PGU_TYPE_TAG_ENTRY(InlineCodeBlock);			/* in nodes/parsenodes.h */
    PGU_TYPE_TAG_ENTRY(FdwRoutine);				/* in foreign/fdwapi.h */
    PGU_TYPE_TAG_ENTRY(IndexAmRoutine);			/* in access/amapi.h */
    PGU_TYPE_TAG_ENTRY(TableAmRoutine);			/* in access/tableam.h */
    PGU_TYPE_TAG_ENTRY(TsmRoutine);				/* in access/tsmapi.h */
    PGU_TYPE_TAG_ENTRY(ForeignKeyCacheInfo);		/* in utils/rel.h */
    PGU_TYPE_TAG_ENTRY(CallContext);				/* in nodes/parsenodes.h */
    PGU_TYPE_TAG_ENTRY(SupportRequestSimplify);	/* in nodes/supportnodes.h */
    PGU_TYPE_TAG_ENTRY(SupportRequestSelectivity);	/* in nodes/supportnodes.h */
    PGU_TYPE_TAG_ENTRY(SupportRequestCost);		/* in nodes/supportnodes.h */
    PGU_TYPE_TAG_ENTRY(SupportRequestRows);		/* in nodes/supportnodes.h */
    PGU_TYPE_TAG_ENTRY(SupportRequestIndexCondition); /* in nodes/supportnodes.h */
    PGU_TYPE_TAG_ENTRY(SupportRequestWFuncMonotonic);	/* in nodes/supportnodes.h */
#endif
};

template<class T>
T cast(::Node* pNode)
{
    return pNode->type == TypeTag::of((T)nullptr) ? reinterpret_cast<T>(pNode) : nullptr;
}

template<class T>
T cast(const ::Node* pNode)
{
    return cast<T>(const_cast<::Node*>(pNode));
}

template<class T>
T cast(::Node& node)
{
    if (node.type != TypeTag::of((typename std::remove_reference<T>::type*)nullptr))
    {
        throw std::bad_cast();
    }

    return reinterpret_cast<T>(node);
}

template<class T>
T cast(const ::Node& node)
{
    return cast<T>(const_cast<::Node&>(node));
}

template<>
inline ::List* cast<::List*>(::Node* pNode)
{
    ::List* pList = nullptr;

    switch (pNode->type)
    {
    case T_List:
    case T_IntList:
    case T_OidList:
        pList = reinterpret_cast<::List*>(pNode);
        break;

    default:
        break;
    }

    return pList;
}

template<>
inline const ::List* cast<const ::List*>(const ::Node* pNode)
{
    return cast<::List*>(const_cast<::Node*>(pNode));
}

template<>
inline ::List& cast<::List&>(::Node& node)
{
    ::List* pList = nullptr;

    switch (node.type)
    {
    case T_List:
    case T_IntList:
    case T_OidList:
        pList = reinterpret_cast<::List*>(&node);
        break;

    default:
        throw std::bad_cast();
    }

    return *pList;
}

template<>
inline const ::List& cast<const ::List&>(const ::Node& node)
{
    return cast<::List&>(const_cast<::Node&>(node));
}

// C++-wrapper for a T_List List. If a need for a T_IntList List and/or
// a T_OidList List is needed, this can be turned into a template.
class NodeList
{
public:
    enum class Location
    {
        BEGIN,
        END
    };

    class iterator
    {
    public:
        explicit iterator()
        {
        }

        explicit iterator(const ::List* pList, Location location)
            : m_pList(pList)
            , m_pos(location == Location::BEGIN ? 0 : m_pList->length)
        {
        }

        iterator& operator++()
        {
            mxb_assert(m_pList);
            mxb_assert(m_pos < m_pList->length);
            ++m_pos;
            return *this;
        }

        iterator operator++(int)
        {
            iterator rv(*this);
            ++(*this);
            return rv;
        }

        bool operator==(const iterator& rhs) const
        {
            mxb_assert(m_pList && m_pList == rhs.m_pList);
            return m_pos == rhs.m_pos;
        }

        bool operator!=(const iterator& rhs) const
        {
            return !(*this == rhs);
        }

        Node* operator*() const
        {
            mxb_assert(m_pList);
            mxb_assert(m_pos < m_pList->length);
            return static_cast<::Node*>(m_pList->elements[m_pos].ptr_value);
        }

    private:
        const ::List* m_pList { nullptr };
        int           m_pos   { -1 };
    };


    NodeList(const ::List* pList)
        : m_pList(pList)
    {
        mxb_assert(pList->type == T_List);
    }

    iterator begin() const
    {
        return iterator(m_pList, Location::BEGIN);
    }

    iterator end() const
    {
        return iterator(m_pList, Location::END);
    }

private:
    const ::List* m_pList { nullptr };
};

}

namespace pgu = pgutils;
