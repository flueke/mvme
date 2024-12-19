
| Case | prefix | dynamic | suffix | description                  | action / output                                       |
| ---- | ------ | ------- | ------ | ---------------------------- | ----------------------------------------------------- |
| 1    | 0      | 0       | 0      | empty input data             | pass one empty event to output?                       |
| 2    | 0      | 1       | 0      | dynamic only (block read)    | split the dynamic part, yield resulting events        |
| 3    | 1      | 0       | 0      | prefix only (single reads)   | yield prefix in the first output event                |
| 4    | 1      | 1       | 0      | prefix + dynamic             | yield prefix in the first output event, split dynamic |
| 5    | 0      | 1       | 1      | dynamic + suffix             | yield suffix in first output event, split dynamic     |
| 6    | 0      | 0       | 1      | not possible, would be 1 0 0 |                                                       |
| 7    | 1      | 0       | 1      | not possible, would be 1 0 0 |                                                       |
