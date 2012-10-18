#include "short_path.h"

void shortest_path(){
    struct list_head *v_pos;
    struct list_head *e_pos;
    struct list_head *v_pos2;
    // for each V
    list_for_each(v_pos, &(rd->nodes.list)){
        struct NodeInfo *iterator = list_entry(v_pos, struct NodeInfo, list);
        // for each E
        list_for_each(e_pos, &(iterator->lsa->entries.list)){
            struct id_list *each_edge = list_entry(e_pos, struct id_list, list);
                // find the corresponding V and compare the cost 
                list_for_each(v_pos2, &(rd->nodes.list)){
                    struct NodeInfo *iterator2 = list_entry(v_pos2, struct NodeInfo, list);
                    if(each_edge->id == iterator2->node_id){
                        if (iterator2->distance > iterator->distance + 1){
                            // a better path found
                            iterator2->distance = iterator->distance +1;
                            iterator2->next_hop = iterator->next_hop;
                        }
                        // work down, leave
                        break;
                    }
                }
        }
    }
}


