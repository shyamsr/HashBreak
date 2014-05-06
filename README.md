HashBreak
=========

HashBreak is a distributed password decoder/cracker which works by trying to hash each member of a character space and compare it with the input hash. If there is a match, it derives a conclusion that the password is decoded. The algorithm works in a distributed way, using worker clients who can join/leave the worker pool at any time. The worker pool is managed by the Server. The code currently works for SHA-1.  



@author
Shyam S Ramachandran
Sindhu Alagesan
