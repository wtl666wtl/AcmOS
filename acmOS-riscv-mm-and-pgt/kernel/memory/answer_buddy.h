// 提示：把元数据页加入到空闲池子里
void _buddy_return_page(page_t* page) {
    // TODO: make merge here
    // Suggested: 4 LoCs
    page->flags = BD_PAGE_FREE;
    bd_lists[page->orders].nr_free++;
    //printk("%u %u\n",page,&page->list_node);
    list_add(&page->list_node, &bd_lists[page->orders].list_head);
    //printk("%u\n",bd_lists[page->orders].list_head.next);
}

// 这个函数可以修改
void _buddy_get_specific_page(page_t* page) {
    page->flags |= BD_PAGE_IN_USE;
    bd_lists[page->orders].nr_free--;
    list_del(&page->list_node);
}

void _buddy_clear_flag(page_t* page) { page->flags = 0; }

// 提示：buddy伙伴页的元数据位于整个内存可分配区域的最前面
// 该函数给出一个buddy伙伴页的元数据指针
// 返回元数据所指向的页是全局从数据区开始的第几个页
uint64 _buddy_get_page_idx(page_t* page) { return (page->addr - bd_meta.data_head) / BD_LEAF_SIZE; }

// 提示：请仔细阅读kfree函数的代码
uint64 _buddy_get_area_idx(void* head) { return (uint64)(head - bd_meta.data_head) / PGSIZE; }

// 提示：给出一个全局页的下标，返回这个页的元数据指针
page_t* _buddy_idx_get_page(uint64 idx) {
    return (page_t*)(bd_meta.meta_head + idx * sizeof(page_t));
}

// 提示：给出一个页的元数据指针，找到这个页的buddy页的元数据指针
// 注意利用buddy页的一些性质
page_t* _buddy_get_buddy_page(page_t* page) {
    // Suggested: 2 LoCs
    uint64  buddy_page_idx = _buddy_get_page_idx(page) ^ (1 << page->orders);
   // printk("%u %u\n",buddy_page_idx, _buddy_idx_get_page(buddy_page_idx));
    return _buddy_idx_get_page(buddy_page_idx);
}

// 提示：给出一个buddy页的元数据，将这个页分裂成至少出现1个target_order的函数
page_t *_buddy_split_page(page_t *original_page, uint64 target_order){
    // Suggested: 5 LoCs
    //printk("%u %u\n",original_page,bd_meta.meta_head);
    //printk("%u %u\n",original_page->orders,target_order);
    if(original_page->orders < target_order)
        BUG("_buddy_split_page error!");
    if(original_page->orders == target_order){
        _buddy_get_specific_page(original_page);
        if(original_page->flags & BD_PAGE_FREE)original_page->flags ^= BD_PAGE_FREE;
        return original_page;
    }
    //printk("%u \n",_buddy_get_page_idx(original_page));
    _buddy_get_specific_page(original_page);
    original_page->orders--;
    page_t *left_page = original_page;
    page_t *right_page = _buddy_get_buddy_page(left_page);
    right_page->orders = left_page->orders;
    //printk("%u %u\n",left_page->addr,right_page->addr);
    //left_page->orders = right_page->orders = original_page->orders - 1;
    _buddy_return_page(left_page);
    _buddy_return_page(right_page);
    return _buddy_split_page(right_page, target_order);
}

// 提示：分配一个order为order的页
page_t *_buddy_alloc_page(uint64 order){
    // Suggested: 13 LoCs
    //return NULL;
    for(uint64 i = order; i < bd_max_size; i++)
        if(bd_lists[i].nr_free > 0){
        //    printk("%u %u\n",bd_lists[i].list_head.next,&bd_lists[i].list_head.next);
            return _buddy_split_page((page_t *)(bd_lists[i].list_head.next), order);
        }
    return NULL;
}

// 这个函数可以修改
void buddy_free_page(page_t* page) {
    page_t* current_page = page;
    for (; current_page->orders < bd_max_size; ++current_page->orders) {
        page_t* buddy_page = _buddy_get_buddy_page(current_page);
        //if(_buddy_get_page_idx(current_page) == 3)printk("%u\n",current_page->addr);
        if ((!(buddy_page->flags & BD_PAGE_FREE))) {
            break;
        }
        //printk("?");
        if(buddy_page->orders != current_page->orders) break;
        page_t* to_be_released = NULL;
        if(_buddy_get_page_idx(current_page) > _buddy_get_page_idx(buddy_page))
            to_be_released = current_page;
        else to_be_released = buddy_page;
        _buddy_get_specific_page(to_be_released);
        _buddy_clear_flag(to_be_released);
        if(_buddy_get_page_idx(current_page) > _buddy_get_page_idx(buddy_page))current_page = buddy_page;
    }
    _buddy_return_page(current_page);
    //if(_buddy_get_page_idx(page)<65536)printk("%u %u\n",_buddy_get_page_idx(current_page),current_page->orders);
}
