# Job System 深度分析

## JobSystem 抽象接口

Jolt 通过 `JobSystem` 抽象接口实现跨平台并行，不使用传统线程池 API：

```cpp
class JobSystem : public NonCopyable {
public:
    class JobHandle : private Ref<Job> {
    public:
        bool   IsValid() const;
        bool   IsDone() const;
        void   AddDependency(int inCount = 1) const;
        void   RemoveDependency(int inCount = 1) const;
        static void sRemoveDependencies(const JobHandle*, uint, int = 1);
        // 内部使用，非公开 API
        using Ref<Job>::GetPtr;
    };

    class Barrier : public NonCopyable {
    public:
        virtual void AddJob(const JobHandle&) = 0;
        virtual void AddJobs(const JobHandle*, uint) = 0;
    protected:
        virtual ~Barrier() = default;
        virtual void OnJobFinished(Job*) = 0;
        friend class Job;
    };

    using JobFunction = function<void()>;

    virtual int        GetMaxConcurrency() const = 0;
    virtual JobHandle  CreateJob(const char* name, ColorArg color,
                                 const JobFunction&,
                                 uint32 numDependencies = 0) = 0;
    virtual Barrier*   CreateBarrier() = 0;
    virtual void       DestroyBarrier(Barrier*) = 0;
    virtual void       WaitForJobs(Barrier*) = 0;
};
```

### 依赖模型

作业通过依赖计数 (dependency counter) 调度：
- 创建时指定 `numDependencies`
- `AddDependency(n)` / `RemoveDependency(n)` 增减计数器
- 计数器归零 → 作业被 `QueueJob` 排队执行

```
first_job = CreateJob("First", color, []{ ... }, 0);  // 立即执行
second_job = CreateJob("Second", color, []{ ... }, 1); // 等待 1 个依赖
first_job_fn() { second_job.RemoveDependency(); }      // 完成 first → 触发 second
```

### Barrier 模式

不同于直接等待 `JobHandle` 数组，Jolt 使用 Barrier 模式：

```cpp
Barrier *barrier = job_system->CreateBarrier();
barrier->AddJob(first_job);
barrier->AddJob(second_job);
barrier->AddJob(third_job);
job_system->WaitForJobs(barrier);
job_system->DestroyBarrier(barrier);
```

关键特性：作业可以在 `WaitForJobs` 等待期间继续动态添加到 Barrier。

## 三种内置实现

### 1. JobSystemSingleThreaded
单线程顺序执行。无并发。用于调试和确定性验证。

### 2. JobSystemThreadPool
生产实现，工作窃取线程池。继承自 `JobSystemWithBarrier`：

```cpp
class JobSystemThreadPool final : public JobSystemWithBarrier {
    // 构造函数内直接初始化
    JobSystemThreadPool(uint maxJobs, uint maxBarriers,
                        int numThreads = -1);
    // 或先默认构造再调用 Init()
    JobSystemThreadPool() = default;
    void Init(uint maxJobs, uint maxBarriers, int numThreads = -1);

    int  GetMaxConcurrency() const;     // = numThreads + 1 (含主线程)
    void SetNumThreads(int);            // 运行时调整线程数
    void SetThreadInitFunction(...);    // 线程初始化回调
    void SetThreadExitFunction(...);    // 线程退出回调
};
```

线程数默认 `-1 → thread::hardware_concurrency() - 1`。

内部实现：
- `FixedSizeFreeList<Job>`: 定长作业池
- 无锁环形作业队列 (1024 容量，2 的幂)
- 每线程维护独立 head，共享 tail
- `Semaphore` 信号通知工作线程
- `mQuit` 原子标志优雅退出

### 3. JobSystemWithBarrier
不是装饰器，是提供 `CreateBarrier`/`DestroyBarrier` 默认实现的基类。
`JobSystemThreadPool` 继承它来复用 Barrier 管理逻辑。

```cpp
class JobSystemWithBarrier : public JobSystem {
protected:
    // 使用 FixedSizeFreeList<BarrierImpl> 管理 Barrier
    virtual void QueueJob(Job*) = 0;     // 仍需子类实现
    virtual void QueueJobs(Job**, uint) = 0;
    virtual void FreeJob(Job*) = 0;
};
```

## 物理管道中的并行化

### 阶段级并行
```
ApplyGravity ──→ ParallelFor(ActiveBodies, batch=64)
                  每个线程独立处理一批刚体

FindCollisions ─→ 动态作业创建
                  BroadPhase 输出 BodyPairs → 队列
                  作业从队列消费 BodyPair → NarrowPhase
                  最大并发数 = GetMaxConcurrency()
                  每批 cNarrowPhaseBatchSize 对

SetupVelocityConstraints ─→ ParallelFor(Constraints, batch=256)

SolveVelocity ──→ ParallelFor(Islands)
                  岛间并行，岛内串行

IntegrateVelocity ─→ ParallelFor(ActiveBodies, batch=64)
```

### 作业调度树
```
PhysicsSystem::Update
├── (barrier) JobBroadPhasePrepare (后台)
├── (barrier) JobStepListeners
├── (barrier) JobDetermineActiveConstraints
├── (barrier) JobApplyGravity
├── JobFindCollisions[]         ← 多个并行作业
│   └── (共享 BodyPairQueue，动态生成)
├── JobUpdateBroadphaseFinalize
├── (barrier) JobSetupVelocityConstraints
├── JobBuildIslandsFromConstraints
├── JobFinalizeIslands
├── JobBodySetIslandIndex
├── JobSolveVelocityConstraints[] ← 岛级并行
├── JobPreIntegrateVelocity
├── (barrier) JobIntegrateVelocity
├── JobPostIntegrateVelocity
├── JobResolveCCDContacts
├── (barrier) JobSolvePositionConstraints
├── JobContactRemovedCallbacks
├── SoftBody 作业
└── JobStartNextStep → 链式触发下一步
```

## 线程安全

### BodyAccess 权限系统
```cpp
namespace BodyAccess {
    enum class EAccess { Read, ReadWrite };
    // Thread Local Storage 检查当前线程权限
    static bool sCheckRights(EAccess, EAccess);
}
```

### MutexArray
细粒度分段锁，基于 body_id 取模：
```cpp
class MutexArray {
    // 锁数: 2 的幂，范围 [1, 64]
    // 锁选择: mutexes[body_id & (numMutexes - 1)]
};
```

## 性能要点

- `GetMaxConcurrency()` 返回 `线程数 + 1` (含主线程)，无硬编码上限
- `cMaxPhysicsJobs = 2048` / `cMaxPhysicsBarriers = 8`
- Barrier 不宜过多 (每次需同步所有线程)
- `LargeIslandSplitter` 可并行化大岛 (默认开启)
- `JobSystemThreadPool` 是"示例实现" — 推荐用户对接自有引擎的作业系统
- BodyPair 队列有缓存行填充避免 false sharing
