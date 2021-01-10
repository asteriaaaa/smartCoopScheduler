# smartCoopScheduler
This is an early version of implementation of the paper "Efficient algorithms for task mapping on heterogeneous CPU/GPU platforms for fast completion time".

## Step 1 - Preparation

We use [StarPU](https://starpu.gitlabpages.inria.fr/) (version 1.2.7) as platform and modify the source code of StarPU. Firstly, you need to prepare the source code of StarPU and install the dependencies.

## Step 2 - How to implement scheduler?

We implement the  the customized scheduler from StarPU by modify `src/sched_policies`. The StarPU implement some basic scheduling policies (e.g., in random_policy.c) and state-of-the-art policies (e.g., in work_stealing_policy.c). You can implement the scheduler here.

## Step 3 - How to add the scheduler in StarPU?

We add the customized scheduler from StarPU by modify `src/core/sched_policy.c/.h`. You can find the pre-defined scheduling policy in StarPU in a static struct `predefined_policies[]` in `sched_policy.c`. Similarly, you can customize policies and implement it on StarPU by adding the hook to this struct. If you add new files, please make sure to modify `src/Makefile.am` to pass compilation.

## Step 4 - How to use the customized scheduler?

After installing StarPU, you can find an environment variable named `sched_policy`(or some similar name). This environment variable is used to switch active scheduler, if unset it and run benchmarks, StarPU will use default `eager` scheduling policy. Set the name of customized scheduler to this environment variable can assign it to be the currently active scheduler. 

Also, you may use additional argumentations when running benchmarks in StarPU to temporarily use  targeted scheduler.

## Step 5 - How to implement customized benchmark?

StarPU offers a variety of benchmarks to evaluate runtime performance of tasks. The source code of these benchmark is located in `examples/`. You can implement the benchmark here. If you add new files, please make sure to modify `examples/Makefile.am` to pass compilation.





Please cite the paper in your publications if it helps your research (please also cite StarPU if it helps your research):

```
@article{LI2020101936,
	title = "Efficient algorithms for task mapping on heterogeneous CPU/GPU platforms for fast completion time",
	journal = "Journal of Systems Architecture",
	pages = "101936",
	year = "2020",
	issn = "1383-7621",
	doi = "https://doi.org/10.1016/j.sysarc.2020.101936",
	url = "http://www.sciencedirect.com/science/article/pii/S1383762120301934",
	author = "Zexin Li and Yuqun Zhang and Ao Ding and Husheng Zhou and Cong Liu",
	keywords = "GPU, Heterogeneous scheduling, Data-size-based prediction, Neural network runtime acceleration"
}
```

# StarPU

StarPU is a task programming library for hybrid architectures

1. The application provides algorithms and constraints
   - CPU/GPU implementations of tasks
   - A graph of tasks, using either StarPU's rich C/C++ API, or OpenMP pragmas.
2. StarPU handles run-time concerns
   - Task dependencies
   - Optimized heterogeneous scheduling
   - Optimized data transfers and replication between main memory and discrete memories
   - Optimized cluster communications

Rather than handling low-level issues, programmers can concentrate on algorithmic concerns!

```latex
@article{https://doi.org/10.1002/cpe.1631,
	author = {Augonnet, Cédric and Thibault, Samuel and Namyst, Raymond and Wacrenier, Pierre-André},
	title = {StarPU: a unified platform for task scheduling on heterogeneous multicore architectures},
	journal = {Concurrency and Computation: Practice and Experience},
	volume = {23},
	number = {2},
	pages = {187-198},
	keywords = {GPU, multicore, accelerator, scheduling, runtime system},
	doi = {https://doi.org/10.1002/cpe.1631},
	url = {https://onlinelibrary.wiley.com/doi/abs/10.1002/cpe.1631},
	eprint = {https://onlinelibrary.wiley.com/doi/pdf/10.1002/cpe.1631},
	year = {2011}
}
```