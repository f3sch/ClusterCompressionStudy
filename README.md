# ClusterCompressionStudy

Code was mostly written by claude.

Two patches are added for alidist (dependecies) and O2 (cluster binary dump).

##
```bash
~/git/alice/sw/BUILD/ClusterCompressionStudy-latest/ClusterCompressionStudy/cluster_bench $(ls ~/scratch/cluster_comp/test/clusters_*.bin | shuf -n 10 | paste -sd ' ' -)
 python3 ~/git/alice/ClusterCompressionStudy/plot.py results.csv
```
