## README
Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,  
All Rights Reserved  

### 1. Build MIO Library
Uncompress the source code package.  
Run the following steps to build MIO library and examples.
1. ``./autogen.sh``
1. ``./configure``  

If you install Motr rpms in a customized directory such as your home
directory or build Motr from source, run configure like this:  

``./configure --with-libmotr=libmotr_directory --with-motr-headers= motr_headers_directory``

If you have Motr source and want to build MIO with the source code, run
configure as below

``./configure --with-motr-src=motr_source_directory``

3. Make  

### 2. Run MIO examples  
MIO source code is shipped with varied examples.  

mio_rw_threads starts specified number of READ/WRITE threads. WRITE threads
create MIO objects and write data to them, and then inform the READ threads
to read the written objects. READ threads will verify the data fetched from
objects.  

Run mio_rw_threads following the steps below:  
1. Start Motr (follow the Cortx-motr instruction)
1. cd tests

MIO provides a sample configuration file *mio_config.yaml* in this directory.  
Edit *mio_config.yaml* to reflect Motr configurations in your system:  
replace the Motr client local address, MOTR_INST_ADDR, to yours.

<ol type="a">
  <li>replace the Motr client local address, MOTR_INST_ADDR, to yours.</li>
  <li>replace MOTR_HA_ADDR with your HA address.</li>
  <li>MOTR_PROFILE and MOTR_PROCESS_FID (usually no need to change these 2 parameters)</li>
  <li>replace the MOTR_USER_GROUP with the Motr user group in your system.</li>
  <li>replace the MOTR_USER_GROUP with the Motr user group in your system.</li>
  <li>To turn on ADDB for telemetry data generation, set MIO_TELEMETRY_STORE to
    ADDB.</li>
</ol>   

Methods to get the above parameters:
<ol type="a">
  <li>hctl motr status</li>
  <li>checkout /etc/motr/sys-*/conf.xc to search strings for M0_CST_HA</li>
  <li>if your system is shipped with Motr sample apps, run:  

  `` motr-sample-app-dir/scripts/c0appzrcgen ``
  </li>
  </ol>
3. add your account to Motr group then run mio_rw_thread or run it as root:  

``sudo ../examples/mio_rw_threads -s 4096 -c 1 -n 10 -t 1 -y ./mio_config.yaml -o 1:12346800``  

Usage of mio_rw_thread  
<table>  
  <tr>  
    <td width=57px>-o, </td>
    <td width=120px>--object </td>
    <td>FID     </td>
    <td>Starting object ID  </td>
  </tr>

  <tr>  
      <td>-n, </td>
      <td>--nr_objs </td>
      <td> INT </td>
      <td>The number of objects  </td>
  </tr>

  <tr>  
      <td>-s, </td>
      <td>--block-size  </td>
      <td> INT </td>
      <td>block size in bytes or with suffix b/k/m/g/K/M/G </td>
  </tr>

  <tr>  
      <td>-c, </td>
      <td>--block-count  </td>
      <td> INT </td>
      <td>number of blocks written to an object with suffix b/k/m/g/K/M/G</td>
  </tr>

  <tr>  
      <td>-t, </td>
      <td>--threads    </td>
      <td>  </td>
      <td>Number of threads </td>
  </tr>

  <tr>  
      <td>-y, </td>
      <td>--mio_con </td>
      <td>  </td>
      <td>MIO YAML configuration file</td>
  </tr>

  <tr>  
      <td>-h, </td>
      <td>--help </td>
      <td>  </td>
      <td>shows this help text and exit </td>
  </tr>

</table>  

All MIO examples use 2 uint64_t to represent an object ID.
4. MIO also provides with test scripts to test basic functionalities.  
Run the scripts as root or users of Motr group as above.  
``sudo ./mio_run_tests.sh``  

5. Quick Tests on Sage platform  
```
>>cd gitlab/mio/  
>>git pull
>>./autogen.sh  
>>./configure  
>>make  
>>cd tests/
>>motraddr.sh --mio > mio_config.yaml  
>>../examples/mio_rw_threads -s 4096 -c 1 -n 10 -t 1 -y ./mio_config.yaml -o 1:12346800   
```  
<table>  
  <tr>  
    <td width=110px>a:12346800 </td>
    <td width=320px>7750c5e4c1549d15bdbc9690587f0c8b </td>
    <td width=320px>7750c5e4c1549d15bdbc9690587f0c8b     </td>

  </tr>

  <tr>  
      <td>9:12346800 </td>
      <td>cf99430f1feb3b7fa3eb03e396fc509a </td>
      <td> cf99430f1feb3b7fa3eb03e396fc509a </td>

  </tr>

  <tr>  
      <td>8:12346800 </td>
      <td>f22766e0ae86c0d0777747835b749390  </td>
      <td> f22766e0ae86c0d0777747835b749390 </td>

  </tr>

  <tr>  
      <td>7:12346800 </td>
      <td>23ba669b5ac4478682f273a13f5db5d6 </td>
      <td> 23ba669b5ac4478682f273a13f5db5d6 </td>

  </tr>

  <tr>  
      <td>6:12346800</td>
      <td>528f642a32ef56c36f987049ac42e46c </td>
      <td> 528f642a32ef56c36f987049ac42e46c </td>

  </tr>

  <tr>  
      <td>5:12346800</td>
      <td>d8b5aea301d4e653e7ebd610fcf56a2f </td>
      <td> d8b5aea301d4e653e7ebd610fcf56a2f </td>

  </tr>

  <tr>  
      <td>4:12346800</td>
      <td>d6f35954ae4a33961d1e8809000f4d59 </td>
      <td> d6f35954ae4a33961d1e8809000f4d59 </td>

  </tr>

  <tr>  
      <td>3:12346800</td>
      <td>e302c06452fd87119c996fa911373894</td>
      <td> e302c06452fd87119c996fa911373894 </td>

  </tr>

  <tr>  
      <td>2:12346800</td>
      <td>a133a792799f49fe7807c34fc25d393c</td>
      <td> a133a792799f49fe7807c34fc25d393c </td>

  </tr>

  <tr>  
      <td>1:12346800</td>
      <td>f3a95aeb5b0005ca305999bac7a75577</td>
      <td> f3a95aeb5b0005ca305999bac7a75577 </td>

  </tr>
</table>  

[Final Report] 	  Objects TODO: 10    Completed: 10	  Failed: 0	    Matched: 10  

#m0composite
#needed for composite object tests. needs to be runs only once.
#run it anyway and ignore failures.  
#

```
>>motraddr.sh --exp > out.txt
>>cat out.txt
# umanesan1 client-22
# Bash shell export format
export CLIENT_LADDR="172.18.1.22@o2ib:12345:4:8"
export CLIENT_HA_ADDR="172.18.1.21@o2ib:12345:1:1"
export CLIENT_PROFILE="0x7000000000000001:0x4dc"
export CLIENT_PROC_FID="0x7200000000000001:0x151"
>>source ./out.txt
>>m0composite "$CLIENT_LADDR" "$CLIENT_HA_ADDR" "$CLIENT_PROFILE" "$CLIENT_PROC_FID"
```    
<table>  
  <tr>  
    <td>motr[26256]: </td>
    <td>7bd0</td>
    <td>ERROR </td>
    <td>[dix/req.c:794:dix_idxop_meta_update_ast_cb]  </td>
    <td>All items are failed  </td>

  </tr>

  <tr>  
    <td>motr[26256]: </td>
    <td>5bd0 </td>
    <td>ERROR </td>
    <td>[dix/req.c:794:dix_idxop_meta_update_ast_cb]  </td>
    <td>All items are failed  </td>

  </tr>
</table>  

```
>>motraddr.sh --mio > mio_config_p1.yaml
>>motraddr.sh --mio > mio_config_p2.yaml
>>motraddr.sh --mio > mio_config_p3.yaml
>>cat *.yaml | grep MOTR_INST_ADDR
  MOTR_INST_ADDR: 172.18.1.22@o2ib:12345:4:10
  MOTR_INST_ADDR: 172.18.1.22@o2ib:12345:4:14
  MOTR_INST_ADDR: 172.18.1.22@o2ib:12345:4:3
  MOTR_INST_ADDR: 172.18.1.22@o2ib:12345:4:8
>>../examples/mio_comp_obj_example -o 1:123456 -y ./mio_config.yaml
>>./mio_run_tests.sh
/home/users/jusers/umanesan1/sage/gitlab/mio/tests
/home/users/jusers/umanesan1/sage/gitlab/mio
/home/users/jusers/umanesan1/sage/gitlab/mio/examples/  
```  
MIO tests starts:  
Test log will be stored in:
``/home/users/jusers/umanesan1/sage/gitlab/mio/tests/mio_test_sandbox_2021-01-06-12-48-22/mio_test_2021-01-06-12-48-22.log``  

1. [T1] Object creation and deletion tests </li>  

  ``io_create_delete_test:  passed``  

1. [T2] Object IO tests  

  ``io_test_with_sizes_of_multi_4KB:  passed
	io_test_with_sizes_of_multi_non_4KB:  passed
	io_test_in_async:  passed
	io_test_obj_rw_with_lock:  passed
	io_test_with_multi_threads:  passed
	io_test_with_multi_procs:  passed  
  ``  
1. [T3] KVS tests  

  ``kvs_create_query_delete_test:  passed``  
1. Composite object tests  
``obj_hint_test:  passed``

1. Object hint tests  
``mio_run_tests: test status: SUCCESS``
