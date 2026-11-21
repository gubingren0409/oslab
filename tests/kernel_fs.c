/* Included only in FS_TEST builds; exercises the actual block device. */
static uint32 allocated_bits(bool data)
{
    extern super_block_t sb;
    uint32 count=data?sb.data_blocks:sb.total_inodes;
    uint32 first=data?sb.data_bitmap_firstblock:sb.inode_bitmap_firstblock;
    uint32 total=0;
    for (uint32 start=0;start<count;start+=BIT_PER_BLOCK) {
        buffer_t *b=buffer_get(first+start/BIT_PER_BLOCK);
        assert(b!=NULL,"test: bitmap buffer");
        uint32 valid=MIN(count-start,BIT_PER_BLOCK);
        for (uint32 i=0;i<valid;i++) if (b->data[i/8]&(1u<<(i%8))) total++;
        buffer_put(b);
    }
    return total;
}
static void erase_test_inode(inode_t *ip)
{
    inode_lock(ip); ip->disk_info.nlink=0; inode_rw(ip,true); inode_unlock(ip); inode_put(ip);
}
static void kernel_fs_test(void)
{
    uint32 initial_inodes=allocated_bits(false), initial_blocks=allocated_bits(true);
#if FS_TEST == 2
    extern super_block_t sb;
    assert(sb.data_blocks < 3000,"test: use constrained image");
    inode_t *limited=inode_create(INODE_TYPE_DATA,1,1);
    assert(limited!=NULL,"test: constrained inode");
    static char page[PGSIZE]; memset(page,0x5a,PGSIZE);
    inode_lock(limited);
    uint32 written=0;
    for (uint32 i=0;i<sb.data_blocks+1;i++) {
        uint32 n=inode_write_data(limited,written,PGSIZE,page,false);
        if (n==(uint32)-1) break;
        assert(n==PGSIZE,"test: whole page write"); written+=n;
    }
    uint32 pages=written/PGSIZE;
    uint32 indexes=(pages>10)+(pages>1034);
    if (pages>2058) indexes+=1+(pages-2058+1023)/1024;
    assert(allocated_bits(true)==initial_blocks+pages+indexes,"test: failed index allocation rollback");
    assert(inode_write_data(limited,written,PGSIZE,page,false)==(uint32)-1,"test: full disk");
    inode_unlock(limited); erase_test_inode(limited);
    assert(allocated_bits(true)==initial_blocks && allocated_bits(false)==initial_inodes,"test: full-disk recovery");
    printf("LAB8_FULL_PASS pages=%d\n",pages);
    return;
#endif
    inode_t *root=path_to_inode("////");
    assert(root!=NULL,"test: root");
    inode_lock(root); inode_print(root,"root"); dentry_print(root); inode_unlock(root); inode_put(root);
    inode_t *fixture=path_to_inode("////ABCD.txt");
    assert(fixture!=NULL,"test: fixture");
    char sample[32]; inode_lock(fixture);
    assert(inode_read_data(fixture,4090,32,sample,false)==32,"test: fixture read");
    for (int i=0;i<32;i++) assert(sample[i]=='A'+(4090+i)%26,"test: fixture bytes");
    inode_unlock(fixture); inode_put(fixture);

    inode_t *ip=inode_create(INODE_TYPE_DATA,1,1);
    assert(ip!=NULL,"test: create"); inode_dup(ip);
    inode_lock(ip); ip->disk_info.nlink=0; inode_rw(ip,true); inode_unlock(ip); inode_put(ip);
    assert(allocated_bits(false)==initial_inodes+1,"test: delayed delete");
    inode_put(ip);
    assert(allocated_bits(false)==initial_inodes,"test: last reference");

    ip=inode_create(INODE_TYPE_DATA,1,1); assert(ip!=NULL,"test: small create");
    int small[10], output[10]; for (int i=0;i<10;i++) small[i]=i;
    inode_lock(ip);
    for (uint32 i=0;i<400;i++) assert(inode_write_data(ip,i*40,40,small,false)==40,"test: small write");
    assert(inode_read_data(ip,4804,40,output,false)==40,"test: small read");
    for (int i=0;i<10;i++) assert(output[i]==(i+1)%10,"test: small bytes");
    inode_unlock(ip); erase_test_inode(ip);
    printf("LAB8_SMALL_PASS\n");

    ip=inode_create(INODE_TYPE_DATA,1,1); assert(ip!=NULL,"test: big create");
    static char big[4*PGSIZE+1110];
    for (int i=0;i<sizeof(big);i++) big[i]='A'+i%8;
    inode_lock(ip);
    for (uint32 i=0;i<10000;i++) {
        assert(inode_write_data(ip,i*sizeof(big),sizeof(big),big,false)==sizeof(big),"test: big write");
        if (i%1000==999) printf("LAB8_BIG_PROGRESS chunks=%d\n",i+1);
    }
    uint32 offsets[]={0,4090,10*4096-4,1034*4096-4,2058*4096-4,sizeof(big)*10000-32};
    for (int i=0;i<6;i++) {
        assert(inode_read_data(ip,offsets[i],32,sample,false)==32,"test: big read");
        for (int j=0;j<32;j++) assert(sample[j]=='A'+((offsets[i]+j)%sizeof(big))%8,"test: big bytes");
    }
    assert(ip->disk_info.index[10] && ip->disk_info.index[11] && ip->disk_info.index[12],"test: all index tiers");
    inode_print(ip,"big"); inode_unlock(ip); erase_test_inode(ip);
    assert(allocated_bits(true)==initial_blocks,"test: index/data reclamation");
    printf("LAB8_BIG_PASS\n");

    inode_t *dir=inode_create(INODE_TYPE_DIR,1,1);
    inode_t *file=inode_create(INODE_TYPE_DATA,1,1);
    assert(dir && file,"test: directory create");
    root=path_to_inode("/"); inode_lock(root);
    assert(dentry_create(root,dir->inode_num,"AAA")!=(uint32)-1,"test: AAA entry");
    inode_unlock(root);
    inode_lock(dir);
    assert(dentry_create(dir,dir->inode_num,".")!=(uint32)-1,"test: dot");
    assert(dentry_create(dir,ROOT_INODE,"..")!=(uint32)-1,"test: dotdot");
    assert(dentry_create(dir,file->inode_num,"file")!=(uint32)-1,"test: child entry");
    assert(dentry_create(dir,file->inode_num,"file")== (uint32)-1,"test: duplicate");
    assert(dentry_delete(dir,"file")==file->inode_num,"test: delete");
    assert(dentry_create(dir,file->inode_num,"file")==128,"test: reuse slot");
    inode_unlock(dir);
    inode_t *found=path_to_inode("////AAA///file"); assert(found==file,"test: nested path"); inode_put(found);
    found=path_to_parent_inode("/AAA/file",sample); assert(found==dir && !strncmp(sample,"file",5),"test: parent"); inode_put(found);
    found=path_to_inode("/AAA/../ABCD.txt"); assert(found!=NULL,"test: parent traversal"); inode_put(found);
    assert(path_to_inode("/AAA/file/missing")==NULL,"test: regular intermediate");
    char longname[80]; memset(longname,'X',sizeof(longname)); longname[79]=0;
    inode_lock(dir);
    assert(dentry_create(dir,file->inode_num,longname)==(uint32)-1,"test: long filename");
    assert(dentry_delete(dir,"file")==file->inode_num,"test: final entry delete"); inode_unlock(dir);
    inode_lock(root); assert(dentry_delete(root,"AAA")==dir->inode_num,"test: detach"); inode_unlock(root); inode_put(root);
    erase_test_inode(file); erase_test_inode(dir);
    inode_t *held[N_INODE];
    for (int i=0;i<N_INODE;i++) { held[i]=inode_create(INODE_TYPE_DATA,1,1); assert(held[i]!=NULL,"test: cache fill"); }
    assert(inode_create(INODE_TYPE_DATA,1,1)==NULL,"test: cache exhaustion");
    assert(allocated_bits(false)==initial_inodes+N_INODE,"test: cache allocation rollback");
    for (int i=0;i<N_INODE;i++) erase_test_inode(held[i]);
    assert(allocated_bits(false)==initial_inodes && allocated_bits(true)==initial_blocks,"test: complete reclamation");
    buffer_freemem(N_BUFFER);
    printf("LAB8_PASS\n");
}
