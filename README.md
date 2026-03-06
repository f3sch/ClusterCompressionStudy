# ClusterCompressionStudy

Code was mostly written by claude.

Two patches are added for alidist (dependecies) and O2 (cluster binary dump).

##
```bash
~/git/alice/sw/BUILD/ClusterCompressionStudy-latest/ClusterCompressionStudy/cluster_bench $(ls ~/scratch/cluster_comp/test/clusters_*.bin | shuf -n 10 | paste -sd ' ' -)
 python3 ~/git/alice/ClusterCompressionStudy/plot.py results.csv
```


To get the comparision compression ratio for the TopoDict, treat every cluster as huge -> full pattern is written out, then:
unc = TotBytest without Topo
``` bash
root [1] a = o2sim->GetBranch("ITSClusterComp")
root [2] a->GetTotBytes("*")
(long long) 358269474
root [3] b = o2sim->GetBranch("ITSClusterPatt")
root [4] b->GetTotBytes()
(long long) 198774260
```
com = ZipBytes with Topo
```bash
root [4] a = o2sim->GetBranch("ITSClusterComp")
root [5] a->GetZipBytes("*")
(long long) 280954970
root [6] b = o2sim->GetBranch("ITSClusterPatt")
b = o2sim->GetBranch("ITSClusterPatt")
root [7] b->GetZipBytes("*")
(long long) 13719564
```

(358269474 + 198774260) / (280954970 + 13719564) = 1.89
