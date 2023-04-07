 					######################################################################
					SATCHUIM: SAT-based Closed High Utility Itemset Mining 
					Coded by Said Jabbour, Amel Hidouri,   e-mail:{jabbour,hidouri}@cril.fr 
					######################################################################


The code in this repository was written in  C++ using miniSAT solver for model enumeration.


1. Problem Definition
2. Input File Format
3. How to Execute
4. Example


################################
####   Problem Definition   ####
################################

Let I be a set of items. An itemset is a subset of I. Let D be a transaction
database such that each record (called transaction) is an itemset. Frequency
of an itemset is the number of transactions including the itemset. For a
given a minimum support threshold minsupp (called support), an itemset is said to be frequent if its frequency is no less than minsupp.
Each item in the database is associated two weights:
- an internal weight called internal utility, e.g. quantity.
- an external weight called external utility, e.g. unit profit.
The utility of an item is the product of its internal by its external utility. Furthermore, the utility of an itemset is the sum
of utilities of  each item including in that itemset.
An itemset is called High Utility Itemset if its utility is greater than a given minimum utility threshold minutil.


################################
####   Input File Format  ####
################################


A  transactional data is  in the form of a text file with each line representing a transaction (items : TWU : utilities)  and consists of:
-  A set of items represented by integers.
- The total utility of items in this transaction.
- The utility of each item of this transaction.

The input file is a CNF where each line ends with a 0 (zero). This is an example of  a  line in the cnf input file:
1 2 3 4  -1 26 -1 5 6 7 8 0

which means that:

The items are 1 2 3 4

the total utility of items is 26

The items utilities are  5 6 7 8

The three fields are separated by -1.

Remark: In typical transaction database for utility mining (found in SPMF https://www.philippe-fournier-viger.com/spmf/index.php?link=datasets.php) the format is quite different from the one that we use  here (here an example of a line 1 2 3 4:26:5 6 7 8). So, one needs just to replace ':' by '-1'.


################################
####   How to Execute  ####
################################


1. Unzip the file
2. cd core
3.make
4. ./SATCHUIM -minutil=mintuil_threshold -closed=0/1 -verb=ver cnf_filename

   ==== Command Line Options  ====

    -"cnf_filename" is the filename of the input transaction database.
    -minutil: the value of the minimum utility threshold (an integer)
    -closed: 1:for mining closed high utility itemsets; 0:all itemsets (without closedness constraint)
    -verb=verbosity: 1: Default; 3:to display all itemsets
    -cnf_file: input file: it must be a CNF

################################
####   Example  ####
################################
- to find all closed high utility itemsets in "chess.txt" in instance folder,  for minimum utility no less than 650000:

 ./SATCHUIM -minutil=650000 -closed=1 ../instance/chess.txt 

the output is:

**********************************************************************************************************************************************************
 ===============================================[ Problem Statistics ]==================================================
<> SATCHUIM : SAT-based Closed High Utility Itemset Mining
<> minUtil : 650000 
<> instance : ../instance/chess.txt
<> closed? : 1 

---------------------------------------------------------------------------------------------------
  DataBase Description          |-- #items    :         75       
                                |-- #transactions    :       3196      
---------------------------------------------------------------------------------------------------
 =======================================================================================================================
---------------------------------------------------------------------------------------------------
                               |-- #patterns  :              46   
  SAT's Output                 |-- #conflicts  : 1466       
                               |-- #clauses  :         4000404   
---------------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------
                                |-- CPU time    : 0.813679 s 
                                |-- #restarts   : 1
  More statistics               |-- #conflicts  : 1466          (1802 /sec) 
                                |-- #decisions  : 1508          (0.00 % random) (1853 /sec) 
                                |-- #propagations : 3370583       (4142399 /sec) 
                                |-- #conflict literals   : 0             (-nan % deleted) 
---------------------------------------------------------------------------------------------------
