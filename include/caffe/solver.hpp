#ifndef CAFFE_OPTIMIZATION_SOLVER_HPP_
#define CAFFE_OPTIMIZATION_SOLVER_HPP_

#include <string>
#include <vector>
#include <set>

#include "lazy-table-module.hpp"

#include "caffe/net.hpp"

namespace caffe {

struct PsConfig {
  bool no_ps;
  int worker_id;
  int num_workers;
  int slack;
  int batches_per_clock;
  LazyTableConfig lt_config;
  PsConfig() : no_ps(false), slack(0), batches_per_clock(1) {}
};

struct RowAccessInfo {
  vector<uint> row_ids;
  int num_vals;
  bool data_in_mem;  /* Volatile field only used at virtual iteration */
  int data_handle;  /* Volatile field only used at virtual iteration */
};

struct ParamInfo {
  int global_param_id;
  int val_offset;
};

struct ImbInfo {
  int global_imb_id;
  bool fetch;
  bool keep;
  ImbInfo(int g = -1, bool f = false, bool k = false) :
      global_imb_id(g), fetch(f), keep(k) {}
};

struct FetchKeep {
  bool fetch;
  bool keep;
  FetchKeep(bool f = false, bool k = false) : fetch(f), keep(k) {}
};

struct LayerHandles {
  int read_handle;
  int postread_handle;
  int bw_read_handle;
  int bw_postread_handle;
  int prewrite_handle;
  int write_handle;
  int history_access_handle;
  int history_postaccess_handle;
  vector<int> imbs_to_access_fw;
  vector<int> imbs_to_release_fw;
  vector<int> imb_diffs_to_access_fw;
  vector<int> imb_diffs_to_release_fw;
  vector<int> imbs_to_access_bw;
  vector<int> imbs_to_release_bw;
  vector<int> imb_diffs_to_access_bw;
  vector<int> imb_diffs_to_release_bw;
};

typedef std::map<int, FetchKeep> IntSet;
struct LayerInfo {
  int table_id;
  vector<uint> row_ids;
  vector<uint> history_data_row_ids;
  int num_vals;
  vector<ParamInfo> param_infos;
  IntSet imbs_used_fw;
  IntSet imb_diffs_used_fw;
  IntSet imbs_used_bw;
  IntSet imb_diffs_used_bw;
  vector<ImbInfo> imbs_to_access_fw;
  vector<ImbInfo> imbs_to_release_fw;
  vector<ImbInfo> imb_diffs_to_access_fw;
  vector<ImbInfo> imb_diffs_to_release_fw;
  vector<ImbInfo> imbs_to_access_bw;
  vector<ImbInfo> imbs_to_release_bw;
  vector<ImbInfo> imb_diffs_to_access_bw;
  vector<ImbInfo> imb_diffs_to_release_bw;
  int param_size;
  int imb_size;
  vector<LayerHandles> layer_handles;
  double fw_read_time;
  double fw_compute_time;
  double fw_write_time;
  double bw_read_time;
  double bw_compute_time;
  double bw_write_time;
};

/**
 * @brief An interface for classes that perform optimization on Net%s.
 *
 * Requires implementation of ApplyUpdate to compute a parameter update
 * given the current state of the Net parameters.
 */
template <typename Dtype>
class Solver {
 public:
  explicit Solver(const SolverParameter& param, const PsConfig& ps_config);
  explicit Solver(const string& param_file);
  void Init(const SolverParameter& param);
  void InitTrainNet();
  void InitTestNets();
  void InitPs();
  // The main entry of the solver function. In default, iter will be zero. Pass
  // in a non-zero iter number to resume training for a pre-trained net.
  virtual void Solve(const char* resume_file = NULL);
  inline void Solve(const string resume_file) { Solve(resume_file.c_str()); }
  void Step(int iters);
  // The Restore function implements how one should restore the solver to a
  // previously snapshotted state. You should implement the RestoreSolverState()
  // function that restores the state from a SolverState protocol buffer.
  void Restore(const char* resume_file);
  virtual ~Solver() {}
  inline shared_ptr<Net<Dtype> > net() { return net_; }
  inline const vector<shared_ptr<Net<Dtype> > >& test_nets() {
    return test_nets_;
  }
  int iter() { return iter_; }

 protected:
  // Make and apply the update value for the current iteration.
  virtual void ApplyUpdate() = 0;
  virtual Dtype ForwardBackwardUsingPs(const vector<Blob<Dtype>* > & bottom,
      const shared_ptr<Net<Dtype> >& net, bool test) = 0;
  // The Solver::Snapshot function implements the basic snapshotting utility
  // that stores the learned net. You should implement the SnapshotSolverState()
  // function that produces a SolverState protocol buffer that needs to be
  // written to disk together with the learned net.
  void Snapshot();
  // The test routine
  void TestAll();
  void Test(const int test_net_id = 0);
  virtual void SnapshotSolverState(SolverState* state) = 0;
  virtual void RestoreSolverState(const SolverState& state) = 0;
  void DisplayOutputBlobs(const int net_id);

  SolverParameter param_;

  PsConfig ps_config_;
  vector<RowAccessInfo> imb_data_infos_;
  vector<RowAccessInfo> imb_diff_infos_;
  vector<LayerInfo> layer_infos_;
  vector<Blob<Dtype>*> test_net_output_blobs_;

  int iter_;
  int current_step_;
  shared_ptr<Net<Dtype> > net_;
  vector<shared_ptr<Net<Dtype> > > test_nets_;

  shared_ptr<LazyTableModule> ps_;

  DISABLE_COPY_AND_ASSIGN(Solver);
};


/**
 * @brief Optimizes the parameters of a Net using
 *        stochastic gradient descent (SGD) with momentum.
 */
template <typename Dtype>
class SGDSolver : public Solver<Dtype> {
 public:
  explicit SGDSolver(const SolverParameter& param, const PsConfig& ps_config)
      : Solver<Dtype>(param, ps_config) { PreSolve(); }
  explicit SGDSolver(const string& param_file)
      : Solver<Dtype>(param_file) { PreSolve(); }

  const vector<shared_ptr<Blob<Dtype> > >& history() { return history_; }

 protected:
  void PreSolve();
  Dtype GetLearningRate();
  virtual void ApplyUpdate();
  virtual Dtype ForwardBackwardUsingPs(const vector<Blob<Dtype>* > & bottom,
      const shared_ptr<Net<Dtype> >& net, bool test);
  virtual void Normalize(int param_id);
  virtual void Regularize(int param_id);
  virtual void ComputeUpdateValue(int param_id, Dtype rate);
  virtual void ClipGradients();
  virtual void SnapshotSolverState(SolverState * state);
  virtual void RestoreSolverState(const SolverState& state);
  // history maintains the historical momentum data.
  // update maintains update related data and is not needed in snapshots.
  // temp maintains other information that might be needed in computation
  //   of gradients/updates and is not needed in snapshots
  vector<shared_ptr<Blob<Dtype> > > history_, update_, temp_;

  DISABLE_COPY_AND_ASSIGN(SGDSolver);
};

template <typename Dtype>
class NesterovSolver : public SGDSolver<Dtype> {
 public:
  explicit NesterovSolver(
      const SolverParameter& param, const PsConfig& ps_config)
      : SGDSolver<Dtype>(param, ps_config) {}
  explicit NesterovSolver(const string& param_file)
      : SGDSolver<Dtype>(param_file) {}

 protected:
  virtual void ComputeUpdateValue(int param_id, Dtype rate);

  DISABLE_COPY_AND_ASSIGN(NesterovSolver);
};

template <typename Dtype>
class AdaGradSolver : public SGDSolver<Dtype> {
 public:
  explicit AdaGradSolver(
      const SolverParameter& param, const PsConfig& ps_config)
      : SGDSolver<Dtype>(param, ps_config) { constructor_sanity_check(); }
  explicit AdaGradSolver(const string& param_file)
      : SGDSolver<Dtype>(param_file) { constructor_sanity_check(); }

 protected:
  virtual void ComputeUpdateValue(int param_id, Dtype rate);
  void constructor_sanity_check() {
    CHECK_EQ(0, this->param_.momentum())
        << "Momentum cannot be used with AdaGrad.";
  }

  DISABLE_COPY_AND_ASSIGN(AdaGradSolver);
};

template <typename Dtype>
Solver<Dtype>* GetSolver(
    const SolverParameter& param, const PsConfig& ps_config) {
  SolverParameter_SolverType type = param.solver_type();

  switch (type) {
  case SolverParameter_SolverType_SGD:
      return new SGDSolver<Dtype>(param, ps_config);
  case SolverParameter_SolverType_NESTEROV:
      return new NesterovSolver<Dtype>(param, ps_config);
  case SolverParameter_SolverType_ADAGRAD:
      return new AdaGradSolver<Dtype>(param, ps_config);
  default:
      LOG(FATAL) << "Unknown SolverType: " << type;
  }
  return (Solver<Dtype>*) NULL;
}

template <typename Dtype>
Solver<Dtype>* GetSolver(
    const SolverParameter& param) {
  SolverParameter_SolverType type = param.solver_type();
  PsConfig ps_config;
  ps_config.no_ps = true;

  switch (type) {
  case SolverParameter_SolverType_SGD:
      return new SGDSolver<Dtype>(param, ps_config);
  case SolverParameter_SolverType_NESTEROV:
      return new NesterovSolver<Dtype>(param, ps_config);
  case SolverParameter_SolverType_ADAGRAD:
      return new AdaGradSolver<Dtype>(param, ps_config);
  default:
      LOG(FATAL) << "Unknown SolverType: " << type;
  }
  return (Solver<Dtype>*) NULL;
}

}  // namespace caffe

#endif  // CAFFE_OPTIMIZATION_SOLVER_HPP_
