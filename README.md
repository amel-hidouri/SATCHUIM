
# SATCHUIM: SAT-based Closed High Utility Itemset Mining  
**Developed by:** Said Jabbour, Amel Hidouri  
**Contact:** {jabbour,hidouri}@cril.fr  

---

## Table of Contents
1. [Problem Definition](#problem-definition)  
2. [Input File Format](#input-file-format)  
3. [How to Execute](#how-to-execute)  
4. [Example](#example)  

---

## Problem Definition  

Let **I** be a set of items. An **itemset** is a subset of **I**. Let **D** be a transaction database where each record (transaction) is an itemset.  
Each item in the database is associated with two weights:  
1. **Internal utility:** e.g., quantity.  
2. **External utility:** e.g., unit profit.  

The utility of an item is the product of its internal and external utility. The utility of an itemset is the sum of the utilities of its items.  
An itemset is called a **High Utility Itemset (HUI)** if its utility is greater than a given minimum utility threshold (**minutil**).  

---

## Input File Format  

A transactional dataset is stored as a text file where each line represents a transaction in the format:  
```
items : TWU : utilities
```

- **items:** A set of integers representing the items in the transaction.  
- **TWU:** The total utility of the items in the transaction.  
- **utilities:** A list of integers representing the utility of each item in the transaction.  

### CNF Input File Example  
In the CNF file, fields are separated by `-1`, and each line ends with `0`.  

Example:  
```
1 2 3 4 -1 26 -1 5 6 7 8 0
```

This line represents:  
- **Items:** `1 2 3 4`  
- **Total utility (TWU):** `26`  
- **Utilities:** `5 6 7 8`  

**Note:** For datasets from SPMF ([link](https://www.philippe-fournier-viger.com/spmf/index.php?link=datasets.php)), replace `:` with `-1` to match the required format.  

---

## How to Execute  

1. Unzip the tool package.  
2. Navigate to the `core` directory.  
3. Compile the code: `make`  
4. Ensure the executable has the correct permissions: `chmod +x ./SATCHUIM`  
5. Execute the tool:  
   ```bash
   ./SATCHUIM -minutil=mintuil_threshold -closed=0/1 -verb=ver cnf_filename
   ```

### Command Line Options  
- **cnf_filename:** The filename of the input transaction database.  
- **-minutil:** Minimum utility threshold (integer).  
- **-closed:**  
  - `1`: Mine closed high utility itemsets.  
  - `0`: Mine all itemsets (no closedness constraint).  
- **-verb:** Verbosity level:  
  - `1`: Default.  
  - `3`: Display all itemsets.  

---

## Example  

To find all closed high utility itemsets in `chess.txt` with a minimum utility threshold of `650000`, use:  
```bash
./SATCHUIM -minutil=650000 -closed=1 ../instance/chess.txt
```

### Sample Output  
```plaintext
==============================================[ Problem Statistics ]==================================================
<> SATCHUIM: SAT-based Closed High Utility Itemset Mining
<> minUtil  : 650000 
<> instance : ../datasets/chess.txt
<> closed?  : 1 

---------------------------------------------------------------------------------------------------
  DataBase Description          |-- #items          :         75       
                                |-- #transactions   :       3196      
---------------------------------------------------------------------------------------------------
  #patterns                     | 46  
  CPU time                      : 0.671154 s
SATISFIABLE
```

---

### Additional Notes  
- This tool was compiled and tested on an **Ubuntu** system.  
- Ensure that the input file paths are correct and accessible.  
- Use `chmod` to make the tool executable if needed.  
