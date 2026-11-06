# FoodLoop on the real Robin Hood Army case (Lucknow) – base paper's case study

Data: Table B-2 of the base paper's thesis appendix (25 pickup points, 29 delivery sites, 3 volunteer hubs, real coordinates, service times and demands). Fleet, hiring costs, penalty and 4.5 h windows as in the paper. Assumptions L1-L4 (see tools/lucknow_case.py): straight-line km, 20 km/h, return by 6 h. The paper does not state its distance unit, so routing costs are not directly comparable; unmet units and vehicles hired are.

| Scenario | Supply / demand | Method | Objective | Unmet units | Delivered | Two-wheelers | Four-wheelers | Routing cost | Time (s) |
|---|---|---|---|---|---|---|---|---|---|
| base | 1198 / 2585 | **FoodLoop** | 1,389,682 | 1387 | 1198 | 4 | 3 | 182 km | 0.7 |
|  |  | MCMF + direct trips | 1,389,477 | 1387 | 1198 | – | – | 227 km | – |
|  |  | Greedy + routing | 1,389,732 | 1387 | 1198 | – | – | 232 km | – |
|  |  | GA (paper) | 1,391,596 | 1387 | 1198 | 0 | 5 | 2,096 (unit not stated) | 1094 |
|  |  | GA_LS_1 (paper) | 1,391,895 | 1387 | 1198 | 0 | 6 | 1,895 (unit not stated) | 553 |
|  |  | GA_LS_2 (paper) | 1,391,569 | 1387 | 1198 | 1 | 5 | 1,819 (unit not stated) | 2065 |
| S-1 | 2585 / 2585 | **FoodLoop** | 4,616 | 0 | 2585 | 5 | 6 | 366 km | 0.6 |
|  |  | MCMF + direct trips | 5,321 | 0 | 2585 | – | – | 571 km | – |
|  |  | Greedy + routing | 5,021 | 0 | 2585 | – | – | 521 km | – |
|  |  | GA (paper) | 4,832 | 0 | 2585 | 1 | 5 | 2,082 (unit not stated) | 1274 |
|  |  | GA_LS_1 (paper) | 4,553 | 0 | 2585 | 0 | 6 | 1,553 (unit not stated) | 703 |
|  |  | GA_LS_2 (paper) | 4,374 | 0 | 2585 | 0 | 6 | 1,374 (unit not stated) | 2725 |
| S-2 | 2651 / 2585 | **FoodLoop** | 70,696 | 66 | 2585 | 5 | 6 | 446 km | 0.6 |
|  |  | MCMF + direct trips | 71,830 | 66 | 2585 | – | – | 580 km | – |
|  |  | Greedy + routing | 70,992 | 66 | 2585 | – | – | 492 km | – |
|  |  | GA (paper) | 70,773 | 66 | 2585 | 0 | 5 | 2,273 (unit not stated) | 1285 |
|  |  | GA_LS_1 (paper) | 70,393 | 66 | 2585 | 1 | 4 | 2,143 (unit not stated) | 617 |
|  |  | GA_LS_2 (paper) | 70,911 | 66 | 2585 | 1 | 5 | 2,161 (unit not stated) | 2368 |
| S-3 | 1198 / 1198 | **FoodLoop** | 3,110 | 0 | 1198 | 9 | 1 | 360 km | 0.6 |
|  |  | MCMF + direct trips | 3,683 | 0 | 1198 | – | – | 433 km | – |
|  |  | Greedy + routing | 3,390 | 0 | 1198 | – | – | 390 km | – |
|  |  | GA (paper) | 5,118 | 0 | 1198 | 0 | 6 | 2,118 (unit not stated) | 1013 |
|  |  | GA_LS_1 (paper) | 4,545 | 0 | 1198 | 0 | 5 | 2,045 (unit not stated) | 593 |
|  |  | GA_LS_2 (paper) | 4,223 | 0 | 1198 | 0 | 5 | 1,723 (unit not stated) | 2865 |
| S-4 | 1198 / 985 | **FoodLoop** | 215,805 | 213 | 985 | 8 | 1 | 305 km | 0.6 |
|  |  | MCMF + direct trips | 216,084 | 213 | 985 | – | – | 334 km | – |
|  |  | Greedy + routing | 216,351 | 213 | 985 | – | – | 351 km | – |
|  |  | GA (paper) | 216,799 | 213 | 985 | 1 | 3 | 2,049 (unit not stated) | 1070 |
|  |  | GA_LS_1 (paper) | 217,885 | 213 | 985 | 2 | 4 | 2,385 (unit not stated) | 515 |
|  |  | GA_LS_2 (paper) | 215,971 | 213 | 985 | 0 | 3 | 1,471 (unit not stated) | 2666 |
| S-5 | 1198 / 2585 | **FoodLoop** | 1,389,682 | 1387 | 1198 | 4 | 3 | 182 km | 0.5 |
|  |  | MCMF + direct trips | 1,389,731 | 1387 | 1198 | – | – | 231 km | – |
|  |  | Greedy + routing | 1,389,734 | 1387 | 1198 | – | – | 234 km | – |
|  |  | GA (paper) | 3,276,889 | 3273 | 255 | 1 | 4 | 1,639 (unit not stated) | 577 |
|  |  | GA_LS_1 (paper) | 3,061,398 | 3057 | 363 | 0 | 5 | 1,898 (unit not stated) | 684 |
|  |  | GA_LS_2 (paper) | 2,885,026 | 2881 | 451 | 1 | 4 | 1,776 (unit not stated) | 987 |
