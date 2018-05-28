# Expression Operator Help

## Quick syntax guide
* assignment is done using the **`:=`** operator, **not** `=` which performs a
  comparison!
* local variables need to be defined using the `var` keyword
* statements are terminated with a semicolon
* 1d arrays can be created using this syntax:
```
   var a0[3];                     // Default initalized to zero.
   var a1[5] := { 0, 1, 2, 3, 4}; // Explicit init. Note that the dimension
                                  // still needs to be specified on the left
                                  // hand side.
```

* the size of an array can be queried using the `[]` operator:
   `a1[] == 5`
* strings are enclosed in single quotes `'like so'`
* C-style comments are supported as are full line comments starting with either
  `#` or `//`
* To increment a variable use `var_name += 1` as pre- and post-increment (++,
  --) operators are not available.
