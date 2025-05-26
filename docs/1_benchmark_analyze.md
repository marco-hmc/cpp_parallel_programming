## 多线程 benchmark 实验结论

### 1. basic

```
--------------------------------------------------------------------
Benchmark                          Time             CPU   Iterations
--------------------------------------------------------------------
case1_create_threads/100        5.31 ms         3.19 ms          219
case1_create_threads/1000       39.2 ms         24.5 ms           28

case2_task_cost                 79.2 ms         79.2 ms            7
case2_single_thread_cost        1082 ms         1082 ms            1
case2_multi_thread_cost          271 ms        0.544 ms           10

case3_task_cost                 79.5 ms         79.5 ms            7
case3_single_thread_cost        1134 ms         1134 ms            1
case3_multi_thread_cost          343 ms        0.605 ms           10
case3_thread_pool_cost           398 ms          398 ms            2

case4_task_cost                 41.6 ms         41.6 ms           14
case4_single_thread_cost       51636 ms        51635 ms            1
case4_limited_thread_cost      13136 ms        0.730 ms            1
case4_enough_thread_cost       13452 ms         27.4 ms            1

case5_with_lock_cost            1950 ms         1949 ms            1
case5_without_lock_cost         46.3 ms         46.3 ms           15
```

- case1 是测试线程创建速度的，一个线程创建大概就是几微妙。
- case2 是测试伪共享问题的，

### 2.

xcv

0_intro/ 1_algorithims/ 2_ontainers/ 3_graph/ 4_task/ 5_cancel/ 6_performance/
