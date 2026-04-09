# COS568 Systems and Machine Learning

# Project: Learned Index

# Summary

This project is designed to help students study the concept and the implementation of “Learned Index” by understanding a base implementation and using one or more ideas to improve it.
The key idea for “learned index” is that we think of the index as a model that predicts the position of a key in a dataset.  Thus, a learned index can be trained.  Learned indexes typically beat the traditional B+tree by a large factor in both search time and memory footprint.  

We will provide you with the implementations of Dynamic PGM, B+tree and LIPP in C++, and evaluation datasets.  You are required to:

1. Learn these indexes through the codebase, and compare their lookup and insert performance.
2. Improve the Dynamic PGM using a suggested idea or your own ideas. 

**Note: You are allowed and encouraged to use LLMs for coding, but please do not plagiarize others' work.**


# Overview

### What is an Index?

An index is an auxiliary data structure that enables rapid data retrieval by mapping search keys to the locations of records, much like a book’s index directs you to the right page without scanning every line. Traditional indexes—often implemented as B-trees or hash tables—organize data for quick lookups and range queries, reducing search time compared to a full table scan. However, traditional indexes are sometimes still inefficient due to pointer chasing and cache misses. To address these problems, learned indexes leverage machine learning models to approximate the mapping from keys to record positions by learning the underlying data distribution; if the model’s prediction isn’t exact, a small, local search is used to correct it. This innovative approach can yield significant improvements in speed and memory efficiency for static or predictable datasets. **In our experiments, we only index the sorted arrays by default.**

### B+Tree

B+Tree is a well-known traditional index algorithm, which is a balanced, multi-level index data structure widely used in databases and file systems for efficient data retrieval. Unlike binary trees, B+ trees can have many children per node, which minimizes the tree’s height and optimizes disk I/O operations. All the actual data records are stored in the leaf nodes, which are linked sequentially, while the internal nodes serve only as routing indexes containing keys and pointers. (See a brief introduction of B+ Tree [here](https://cs186berkeley.net/notes/note4/)).

 **Lookup in B+Tree**  To perform a lookup, the search starts at the root node. The key is compared against the keys stored in the node to decide which child pointer to follow. This process is repeated recursively through the internal nodes until a leaf node is reached. At the leaf, the key is searched among the stored keys, and if found, the corresponding record is returned. This hierarchical search process generally takes logarithmic time relative to the number of keys.

 **Insertion in B+Tree**  To insert a key, it begins by locating the appropriate leaf node where the new key should reside, using the same search method as a lookup. The new key is then inserted in sorted order within that leaf. If the leaf node has space, the insertion is complete. However, if the node becomes full, it is split into two nodes:

- The keys are divided between the two new nodes.
- The smallest key from the new node is promoted to the parent node as a separator.

If the parent node is full, the split and promotion process may propagate upward, potentially increasing the height of the tree. This split operation ensures that the tree remains balanced, keeping the operations efficient.

### (Dynamic) PGM

 PGM is a learned index, which approximates key distributions with piecewise linear models (See: [The PGM-index: a fully-dynamic compressed learned index with provable worst-case bounds](https://dl.acm.org/doi/abs/10.14778/3389133.3389135)). It is built on Piecewise Linear Approximation (PLA) models that learn the mapping from keys to their positions in a sorted array. In this approach, the key–position relationship is approximated by a sequence of linear segments. Each segment guarantees that, for any key within its range, the predicted position is within a fixed error bound ($\epsilon$) of the true rank. These segments are computed optimally (or near-optimally) to minimize their total number, exploiting the regularities in the key distribution. Furthermore, the PGM-index is organized recursively: the first keys of each segment at one level form the input for constructing a new PLA-model at the next higher level. This process repeats until a single segment (the root) is obtained, resulting in a tree-like structure where each “node” is a lightweight, constant-space linear model.

 **Lookup in PGM / Dynamic PGM**
 Given a key, the PGM-index starts at the root level and uses the corresponding linear model to compute an approximate position. Then, at each subsequent level, it performs a small binary search—restricted to an interval of size roughly 2 $\epsilon$—to identify the next segment that covers k. This recursive descent continues until the final (bottom) level is reached, where a final binary search is conducted in the underlying sorted array to find the exact key.

 **Insertion in Dynamic PGM**
 One key issue for vanilla PGM is that it does not support dynamic workloads such as insertion and deletion. The reason is that each linear segment may cover a variable (and sometimes large) number of keys, meaning the segments are not organized as fixed-size nodes. This variable coverage makes standard techniques (like the node splits and merges used in B-trees) unsuitable. Moreover, updating the optimal PLA-model to maintain the $\epsilon$-guarantee can be computationally expensive when keys are inserted or deleted at arbitrary positions. For these reasons, the original PGM-index does not support dynamic updates efficiently. To address this issue, Dynamic PGM handles insertion following the strategy below:

- When new keys are added at the end of the sorted array, the index first checks whether the new key can be incorporated into the last segment without violating the $\epsilon$ error bound. If so, the update is performed in constant amortized time; if not, a new segment is created, and the update propagates upward through the recursive levels. This strategy ensures that most updates require only a small constant cost per level.
- For updates occurring at arbitrary positions, the Dynamic PGM-index employs a “logarithmic method” inspired by techniques for dynamic arrays. It maintains several buffers (or auxiliary PGM-indexes) of exponentially increasing sizes. When a new key is inserted, it is first added to a small buffer. Once a buffer fills up, it is merged with the next-level buffer, and a new PGM-index is built over the merged, sorted keys.

### LIPP

LIPP, a learned index (See: [Updatable Learned Index with Precise Positions](https://arxiv.org/pdf/2104.05520)), which uses kernelized linear models to accurately predict key positions, avoiding extra searches by resolving conflicts through child node creation and dynamically balancing the tree. Each node in LIPP contains:

1. a model M (often a linear regression) that predicts the position of a key within that node’s sorted range
2. an array of entry slots E, and (3) a bitvector indicating each slot’s type. Entries can be of three types
   1. NULL – an empty gap slot, initially all entries are NULL (free space). New keys can be placed into these gaps.
   2. DATA – a slot holding a key–payload pair (a filled entry).
   3. NODE – a slot that points to a child node (when multiple keys conflict at the same position).

**Lookup in LIPP** To look up a key, LIPP begins at the root node where a learned model computes the exact slot in the node’s array for the key. If the slot contains a direct data entry and the key matches, the lookup returns the associated record immediately. If the slot instead holds a pointer to a child node (indicating that a conflict was previously resolved), the lookup recurses into that node. Because the learned models in each node provide precise positions, no extra in-node searching is needed—resulting in a lookup cost that is proportional only to the height of the tree

**Insertion in LIPP** For LIPP, whenever a new key is inserted, it uses the same model-guided traversal as lookup to find its target position. There are two cases:

1. If the model-directed slot is NULL (empty), the new key is placed into that slot and marked as DATA. This is a straightforward insert into a gap ￼. The model’s prediction remains valid (now producing a DATA entry for that key).
2. If the target slot is already occupied (a DATA entry with another key), it indicates a prediction conflict – two keys mapped to the same position. In this case, LIPP creates a new child node to store the two keys under that slot ￼. The existing key and the new key are both inserted into this new child node (which will have its own array and model covering just that key range). The parent node’s entry is then changed from DATA to NODE type, pointing to the new child node ￼. This effectively splits the slot into a subtree, so each key gets a unique position at the next level.

# Setup

1. We recommend running this project on the  Adroit cluster, in which you can have a 100GB quota to store your data and repository. See [this page](https://researchcomputing.princeton.edu/systems/adroit#How-to-Access-the-Adroit-Cluster) for details on getting access to Adroit. (If you have access to Della, you don't have to request an account for Adroit and can run experiments on Della. When applying for Adroit, please mention "For running experiments in COS568" in your application.)
2. After logging into Adroit, clone this repo and use `module load anaconda3/2023.3` to initialize the python environment.
3. You can use `checkquota` to check your storage quota. The quota contains two parts:
   1. 10GB quota for your home directory (`/home/<your_net_id>`), and
   2. 100GB quota for `/scratch/network/<your_net_id>`. Please run your experiments here.
4. Do not run benchmarks directly on the login/vis nodes. Always submit your experiments as CPU jobs via `sbatch` to ensure exclusive resource access and stable performance.
5. The codebase contains three key parts:
   1. `/benchmarks` contains the source code for building benchmark executables for different indexes.
   2. `/competitors` contains the source code of the implementation of different indexes.
   3. `/scripts` contains the scripts for building the benchmark, generating workload, and result analysis.

# Task 1. Evaluate Lookup and Insertion performance of B+ Tree, Dynamic PGM, and LIPP (10% of Points)

First, to familiarize yourself with the codebase, we provide a set of toy experiments in `scripts/`. All you need to do is execute `scripts/run_all.sh` and understand the structure of the codebase through these scripts. The scripts include:

1. `download_dataset.sh`: Download dataset to `./data`
2. `create_minimal_cmake.sh`: Create CMake file for workload and benchmark generation.
3. `generate_workloads.sh`: Generate workloads. Each workload has the name in the following format: `{dataset}_ops_{operation count}_{range query ratio}rq_{negative lookup ratio}nl_{insert ratio}i_({insert pattern}_)({hotspot ratio}_)({thread number}_)(mix_)({loaded block number}_)({bulk-loaded data size}_)`. Here, negative lookup refers to lookup the keys that do not exist. For example, `books_100M_public_uint64_ops_2M_0.000000rq_0.500000nl_0.500000i_0m` refers to:
   1. The dataset is `books_100M_public`
   2. The total number of operations is 2M
   3. There's no range query (`rq` is 0, and we don't test range query in our experiments)
   4. The ratio of insertion is 0.5 (`i` is 0.5), which means half of the operations will be insertion (1M insertions), and the rest of the operations will be lookup (1M lookups)
   5. The negative lookup ratio is 0.5 (`nl` is 0.5), which means half of the lookup keys (0.5M keys, since we have 1M lookup operations) do not exist in the dataset.

   In `generate_workloads.sh`, we are generating the following four workloads for each dataset (We generate 2M operations for all workloads):
   1. **Lookup-only workload** with `--negative-lookup-ratio 0.5`, i.e, 1M of positive lookup, 1M of negative lookup
   2. **Insert-Lookup workload** with `--insert-ratio 0.5`, `--negative-lookup-ratio 0.5`, i.e., 1M of insertion, 0.5M of positive lookup, 0.5M of negative lookup. In this workload, we first do 1M of insertion, then do 1M of lookup, and the output csv file contains `lookup-throughput` and `insert-throughput`
   3. **Mixed Insert-Lookup workload** with `--insert-ratio 0.9`, `--negative-lookup-ratio 0.5`, i.e., 1.8M of insertion, 1M of positive lookup, 1M of negative lookup. In this workload, the insert and lookup operations are mixed. Therefore, the output csv file only contains the average `throughput` for this hybrid workload.
   4. **Mixed Insert-Lookup workload** with `--insert-ratio 0.1`, `--negative-lookup-ratio 0.5`, i.e., 2M of insertion, 9M of positive lookup, 9M of negative lookup. In this workload, the insert and lookup operations are mixed. Therefore, the output csv file only contains the average `throughput` for this hybrid workload.
4. `build_benchmark.sh`: Generate executable file for benchmark.
5. `run_benchmark.sh`: Run benchmark on four datasets and three indexes.
6. `analysis.py`: Analyze the result and plot the barplot for throughput and indexes.

Here, we are evaluating the lookup (Given a key, how quickly and accurately can the index locate the corresponding value) and Insertion (Add new keys to the index) performance on Facebook, Books, and Osmc datasets, using B+ Tree, Dynamic PGM, and LIPP.

After running all the scripts, you can get the results in `results/*.csv`. We evaluate the following metrics:

1. Lookup: Read Throughput (`read_throughput_mops` in .csv file)
2. Insertion: Insert Throughput (`insert_throughput_mops` in .csv file), and Read Throughput after insertion (`read_throughput_mops` in .csv file)
3. Mixed Insert-Lookup: Mixed Throughput (`mixed_throughput_mops` in .csv file)

In the csv file inside `./results`, for each experiment, we repeat 3 times (by adding `-r 3` in `run_benchmarks.sh`), so you will find three throughput values for each line.
Also, you will find that for each experiment of DynamicPGM and B+Tree, we actually generate three lines of data with different search methods (See Section 3.1 in TLI paper) and values, this is because each index has some hyperparameters. For B+ Tree, the hyperparameters include the `search_method` and `max_node_logsize` (corresponds to `value`, which specifies the maximum node size). For PGM, the hyperparameters include `search_method` and `error_bound` (corresponds to `value`, which specifies the maximum allowed approximation error $\epsilon$. A larger $\epsilon$ usually results in faster bulk loading, but also sacrifices the lookup accuracy. See Figure 2 in the PGM paper). For LIPP, we don’t have hyperparameters so the `search_method` and `value` are left empty. For the indexes with different hyperparameters, you only need to pick on that has the highest average throughput.

**Milestone 1 (Due 3.29):** Compare the Lookup and Insertion average throughput (across 3 runs) among B+Tree, Dynamic PGM, and LIPP. Explain why we can observe such a difference. **It will take ~30mins to run the script.**

# Task 2. Hybrid Dynamic PGM and LIPP (90% of Points)

DynamicPGM is more efficient for amortized insertion, while LIPP is more efficient for lookups. We want to design a hybrid strategy that uses DynamicPGM (DPGM) for insertion and relies on the LIPP index for the majority of lookups. The high-level idea is that whenever DPGM reaches a certain size threshold (for example, 5\% of the total keys), we flush or migrate data from DPGM into LIPP. During the bulk loading phase, data is initially placed into LIPP. We then perform lookups in the (smaller) DPGM, and if an item is not found there, we check the LIPP index. The main concern is that flushing data from DPGM to LIPP (for example, every few million keys) could be expensive, which might defeat the advantages of combining these two approaches.

The second part of this assignment is more open-ended. You are encouraged to propose a creative migration or flushing strategy that can make this process more efficient so that the hybrid approach offers better throughput for mixed workloads than either DPGM or LIPP alone. The challenge is that these indexes have different tree structures: DPGM uses recursive models, while LIPP uses precise positions and conflict-based node creation. LIPP's no error requirement means you cannot directly map from DPGM's approximate models. Furthermore, collecting data from DPGM and inserting it into LIPP could be very costly, and the current LIPP implementation does not support bulk loading if the index already contains data.

One possible solution is to perform the migration process asynchronously, preventing disruption to incoming insertions and lookups. Of course, there may be other strategies worth exploring. You should also perform a hyperparameter sweep to determine how frequently these migrations should occur, accounting for different workload distributions. If the workload is heavily oriented toward lookups, it may not be beneficial to flush data too frequently. Ultimately, you will produce two bar plots on Facebook/Books/Osmc datasets for these workloads, with the y-axis representing throughput:

- Mixed (90\% Lookup, 10\% Insertion): [DPGM, LIPP, HYBRID]
- Mixed (10\% Lookup, 90\% Insertion): [DPGM, LIPP, HYBRID]

To implement the hybrid approach, refer to `competitors/dynamic_pgm_index.h` as an example, then create the following files:

- `benchmarks/benchmark_hybrid_pgm_lipp.h`
- `benchmarks/benchmark_hybrid_pgm_lipp.cc`
- `competitors/hybrid_pgm_lipp.h`

Within these files, you will invoke both LIPP and DPGM. The main challenge is creating an efficient flushing strategy. Once you have done so, you can replicate or modify scripts/minimal/ pipeline such that it is tailored for the two mixed workloads (insertion heavy and lookup heavy), to build and run benchmarks for your new hybrid approach.

**Milestone 2 (30% of Points. Due 4.12):** Implement the hybrid DPGM + LIPP approach. It does not have to outperform the baselines. A naive implementation, extracting data from DPGM and inserting it individually into LIPP, is sufficient. Compare the mixed workload performance of the hybrid approach against DPGM and LIPP. For this milestone, only the Facebook dataset needs to be reported.

Provide a concise page report including bar plots for each workload. Each bar plot should compare DPGM, LIPP, and your hybrid approach. For DPGM, experiment with different hyperparameters and choose the best-performing one. Report throughput and index size for each workload, so four bar plots overall.

**Milestone 3 (60% of Points. Due Dean's Date):** This is the purely open-ended final stage of the project. Your goal is to design and implement an advanced flushing strategy that achieves higher throughput compared to the vanilla DPGM, LIPP, and your own Milestone 2 "Naive" implementation. The submission achieving the highest throughput will receive a 20% bonus point.

1. A possible direction to improve throughput is by flushing data asynchronously from DPGM to LIPP during insertion or lookup operations.

- If you choose this way, be careful about **thread-safety** of LIPP.
- To avoid blocking new insertions during a flush, consider a double-buffered DPGM architecture.
- For this aproach, experiment with how often should the flushing cycle occur, and modifications to the source code of LIPP and DPGM may be required for optimal performance.

2. Reporting Requirements:

- Format: Bar plots (throughput and inde size) for both mixed workloads (10% and 90% Insert) across all three datasets (Books, FB, OSMC), totaling 12 plots. Does not need to be very long. 2-4 pages is proper.
- Precision: Each data point in your plots must be the average of at least 3 repeat runs (-r 3) within a single session.
- No keys are left behind: Your approach must never discard or lose keys during the flushing process. Any implementation that "skips" data to inflate throughput will result in heavy point deductions. No auxiliary data structures are allowed other than LIPP and DPGM.
