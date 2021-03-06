//
//  push.h
//  LightCTR
//
//  Created by SongKuangshi on 2017/12/5.
//  Copyright © 2017年 SongKuangshi. All rights reserved.
//

#ifndef push_h
#define push_h

#include <unordered_map>
#include <atomic>
#include "../common/thread_pool.h"
#include "../common/barrier.h"
#include "../common/network.h"

// Push Grads to PS
template <class TKey, class TValue>
class Push {
    
public:
    Push() : gDelivery(Delivery::Instance()),
             gConsistentHash(ConsistentHash::Instance()) {
    }
    
    void sync(const std::unordered_map<TKey, TValue> &grads) {
        Barrier barrier;
        size_t candidate_ps = 0;
        sendToPS(grads, candidate_ps, [this, &barrier, &candidate_ps]() {
            candidate_ps--;
            assert(candidate_ps >= 0);
            if (candidate_ps == 0) {
                printf("[WORKER Push] ----- %zu complete -----\n", push_seq++);
                barrier.unblock();
            }
        });
        barrier.block();
    }
    
    void async(const std::unordered_map<TKey, TValue> &grads) {
        size_t candidate_ps = 0;
        sendToPS(grads, [this, &candidate_ps]() {
            candidate_ps--;
            assert(candidate_ps >= 0);
            if (candidate_ps == 0) {
                printf("[WORKER Push] ----- %zu complete -----\n", push_seq++);
            }
        });
    }
    
private:
    void sendToPS(const std::unordered_map<TKey, TValue> &grads,
                  size_t& candidate_ps,
                  std::function<void()> callback) {
        auto& push_map_ptr = *tl_map;
        if (push_map_ptr == NULL) {
            push_map_ptr = new std::map<size_t, std::vector<std::pair<TKey, TValue> > >();
        }
        push_map_ptr->clear();
        auto& push_map = *push_map_ptr;
        
        for (auto it = grads.begin(); it != grads.end(); it++) {
            assert(it->second.checkValid());
            if (!it->second.checkPreferredValue()) {
                continue;
            }
            const size_t to_id = BEGIN_ID_OF_PS +
                                 gConsistentHash.getNode(it->first);
            if (push_map.count(to_id) == 0) {
                push_map[to_id] = std::vector<std::pair<TKey, TValue> >();
                candidate_ps++;
            }
            push_map[to_id].emplace_back(*it);
        }
        
        for (auto &item : push_map) {
            const size_t to_id = item.first;
            PackageDescript desc(REQUEST_PUSH);
            for (auto &grad_pair : item.second) {
                desc.content.append(&grad_pair, sizeof(grad_pair)); // push data pair
            }
            desc.callback = [callback](std::shared_ptr<PackageDescript> resp_package) {
                // response without content
                if (callback) {
                    callback();
                }
            };
            gDelivery.send_sync(std::move(desc), to_id);
        }
        
        printf("[WORKER Push] %zu Grad-pairs Sended\n", grads.size());
    }
    
    ThreadLocal<std::map<size_t, std::vector<std::pair<TKey, TValue> > >*> tl_map;
    
    std::atomic<size_t> push_seq{0};
    
    Delivery&& gDelivery;
    ConsistentHash&& gConsistentHash;
};

#endif /* push_h */
