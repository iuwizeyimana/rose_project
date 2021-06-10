# rose_project

This project's goal is to extend the rose compiler for an automated generation of a VIVADO-HLS accelerator for convolutional neural network
Only one pass is used for the optimization and this repo only contains the program used to run that pass
In order to run this project you need to first clone the ROSE repo; more information can be found [here](www.rosecompiler.org)
After cloning ROSE, edit their makefile to include the project.C file 
To run the project type ./project testCode.C in your terminal 
testCode.C being the vanilla C code you want changes in VIVADO-HLS code 
conv.C is an example input file with its example output file being rose_conv.C

Note: The optimization techniques used in this project were derived from [this paper](https://cadlab.cs.ucla.edu/~cong/slides/fpga2015_chen.pdf)
