# Cache Performance Analysis with write policy using SimpleScalar

This project examines how Cache analysis and Performance on Different Policies with replacement write policy using the **SimpleScalar sim-cache** tool.  
Performance is measured based on:
- Hit / Miss percentages
- **Replacement Count (DL1 / IL1)**
- **Write-Back Count (DL1 / IL1)**  
(Note: IL1 is read-only → IL1 Write-Back = **0** always)

for checking write_back policy(default) we modify the cache.h file line no.22 same for  
for checking write_through policy below code is there
#define WRITE_POLICY WRITE_THROUGH /* change to WRITE_THROUGH for comparison */
after change this code do
make clean
make
now you check the policy
analysis command :-
sim-cache -cache il1 il1:<cache size>:<blcok size>:<associativity>:<replacement policy> -cache dl1 dl1:<cache size>:<blcok size>:<associative >:<replacement policy> -o <address of test case>


## System Configurations

| Parameter | Values Tested |
|----------|---------------|
| Block Size | 32B, 64B |
| Associativity | 2-Way, 4-Way |
| Replacement Policy | LRU, FIFO, RANDOM |
| Write Policy | Write-Through (WT), Write-Back (WB) |
| Test Programs | `test-llong`, `test-caselru` |

---

## ✅ Test Case: **test-llong (Associativity = 2)**

| Policy | Write | Block | DL1 Hit% | IL1 Hit% | Replacement (DL1 / IL1) | Write-Back (DL1 / IL1) |
|-------|-------|-------|----------|----------|--------------------------|------------------------|
| LRU | WT | 32 | 0.9514 | 0.8528 | 479 / 4378 | 5461 / 0 |
| LRU | WT | 64 | 0.9743 | 0.9432 | 238 / 1669 | 5461/ 0 |
| LRU | WB | 32 | 0.9514 | 0.8528 | 479 / 4378 | 436 / 0 |
| LRU | WB | 64 | 0.9743 | 0.9432 | 238 / 1669 | 213 / 0 |
| FIFO | WT | 32 | 0.9507 | 0.8526 | 486 / 4384 | 5461/ 0 |
| FIFO | WT | 64 | 0.9745 | 0.9432 | 236 / 1670 | 5461 / 0 |
| FIFO | WB | 32 | 0.9507 | 0.8526 | 486 / 4384 | 444 / 0 |
| FIFO | WB | 64 | 0.9745 | 0.9432 | 236 / 1670 | 212 / 0 |
| RANDOM | WT | 32 | 0.9494 | 0.8649 | 500 / 4018 | 5461/ 0 |
| RANDOM | WT | 64 | 0.9727 | 0.9450 | 255 / 1615 | 5461 / 0 |
| RANDOM | WB | 32 | 0.9494 | 0.8649 | 500 / 4018 | 448 / 0 |
| RANDOM | WB | 64 | 0.9727 | 0.9450 | 255 / 1615 | 221 / 0 |

---

## ✅ test-llong (Associativity = 4)

| Policy | Write | Block | DL1 Hit% | IL1 Hit% | Replacement (DL1 / IL1) | Write-Back (DL1 / IL1) |
|-------|-------|-------|----------|----------|--------------------------|------------------------|
| LRU | WT | 32 | 0.9554 | 0.9357 | 405 / 1863 | 5461 / 0 |
| LRU | WT | 64 | 0.9766 | 0.9640 | 182 / 1015 | 5461 / 0 |
| LRU | WB | 32 | 0.9554 | 0.9357 | 405 / 1863 | 393 / 0 |
| LRU | WB | 64 | 0.9766 | 0.9640 | 182 / 1015 | 176 / 0 |
| FIFO | WT | 32 | 0.9554 | 0.9354 | 405 / 1872 | 5461/ 0 |
| FIFO | WT | 64 | 0.9765 | 0.9634 | 183 / 1032 | 5461 / 0 |
| FIFO | WB | 32 | 0.9554 | 0.9354 | 405 / 1872 | 393 / 0 |
| FIFO | WB | 64 | 0.9765 | 0.9634 | 183 / 1032 | 177 / 0 |
| RANDOM | WT | 32 | 0.9536 | 0.9305 | 424 / 2020 | 5461 / 0 |
| RANDOM | WT | 64 | 0.9761 | 0.9644 | 188 / 1002 | 5461 / 0 |
| RANDOM | WB | 32 | 0.9536 | 0.9305 | 424 / 2020 | 403 / 0 |
| RANDOM | WB | 64 | 0.9761 | 0.9644 | 188 / 1002 | 177 / 0 |

---

## ✅ test-caselru (Associativity = 2)

| Policy | Write | Block | DL1 Hit% | IL1 Hit% | Replacement (DL1 / IL1) | Write-Back (DL1 / IL1) |
|-------|-------|-------|----------|----------|--------------------------|------------------------|
| LRU | WT | 32 | 0.8945 | 0.8876 | 471 / 1043 | 5461 / 0 |
| LRU | WT | 64 | 0.9442 | 0.9372 | 234 / 569 | 5461 / 0 |
| LRU | WB | 32 | 0.8945 | 0.8876 | 471 / 1043 | 437 / 0 |
| LRU | WB | 64 | 0.9442 | 0.9372 | 234 / 569 | 215 / 0 |
| FIFO | WT | 32 | 0.8947 | 0.8874 | 470 / 1045 | 5461 / 0 |
| FIFO | WB | 32 | 0.8947 | 0.8874 | 470 / 1045 | 437 / 0 |
| RANDOM | WT | 32 | 0.8918 | 0.8876 | 484 / 1043 | 5461 / 0 |
| RANDOM | WB | 32 | 0.8918 | 0.8876 | 484 / 1043 | 444 / 0 |

---

## ✅ test-caselru (Associativity = 4)

| Policy | Write | Block | DL1 Hit% | IL1 Hit% | Replacement (DL1 / IL1) | Write-Back (DL1 / IL1) |
|-------|-------|-------|----------|----------|--------------------------|------------------------|
| LRU | WT | 32 | 0.8985 | 0.8994 | 420 / 898 | 5461 / 0 |
| LRU | WT | 64 | 0.9469 | 0.9473 | 189 / 440 | 5461/ 0 |
| LRU | WB | 32 | 0.8985 | 0.8994 | 420 / 898 | 403 / 0 |
| LRU | WB | 64 | 0.9469 | 0.9473 | 189 / 440 | 182 / 0 |
| FIFO | WT | 32 | 0.8987 | 0.8992 | 419 / 900 | 5461 / 0 |
| FIFO | WB | 32 | 0.8987 | 0.8992 | 419 / 900 | 404 / 0 |
| RANDOM | WT | 32 | 0.8943 | 0.9006 | 440 / 887 | 5461 / 0 |
| RANDOM | WB | 32 | 0.8943 | 0.9006 | 440 / 887 | 414 / 0 |

---

## 🏁 Final Conclusion

| Best Configuration |
|-------------------|
| **LRU + Write-Back + 64B Block + 4-Way Associativity** |

✔ Highest Hit Rate  
✔ Lowest Replacement  
✔ Lowest Memory Traffic  

