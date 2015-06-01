#include <sdsl/suffix_arrays.hpp>
#include <vector>
#include <iostream>

using namespace sdsl;
using namespace std;

using namespace std::chrono;
using timer = std::chrono::high_resolution_clock;


template <typename type_csa, typename type_wt>
size_t match_ref(type_csa csa, type_wt wts, string s1, string s2, size_t min_gap, size_t max_gap, std::function<void(typename type_csa::size_type a, typename type_csa::size_type b)> callback)
{
    using size_type = typename type_csa::size_type;

    size_type sp1, ep1;
    auto cnt1 = backward_search(csa, 0, csa.size()-1, s1.begin(), s1.end(), sp1, ep1);

    size_type sp2, ep2;
    auto cnt2 = backward_search(csa, 0, csa.size()-1, s2.begin(), s2.end(), sp2, ep2);

    vector<size_type> p2;

    if (cnt1 == 0 || cnt2 == 0)
        return 0;

    size_type pat_len = s1.size();

    for (auto i2 = sp2; i2 <= ep2; i2++)
        p2.push_back(wts[i2]);

    sort(p2.begin(), p2.end());

    size_t result = 0;
    for (auto i1 = sp1; i1 <= ep1; i1++)
    {
        size_type l1 = wts[i1];
        auto i2a = lower_bound(p2.begin(), p2.end(), l1 + min_gap + pat_len);
        auto i2b = upper_bound(p2.begin(), p2.end(), l1 + max_gap + pat_len);

        for (auto i2 = i2a; i2 < i2b; i2++)
        {
            callback(l1, *i2 + s2.size() - 1);
            result++;
        }
    }
    return result;
}

template<class type_csa, class type_wt>
struct node_cache
{
    typedef typename type_wt::node_type  node_type;
    typedef typename type_csa::size_type size_type;
    
    type_wt *wts;
    node_type node;
    pair<shared_ptr<node_cache>, shared_ptr<node_cache>> children;
    size_type range_begin;
    size_type range_end;
    bool is_leaf;
    
    size_type range_size()
    {
        return range_end - range_begin;
    }
    
    node_cache(node_type node, type_wt *wts)
    {
        this->wts = wts;
        this->node = node;
        this->children = make_pair(nullptr, nullptr);
        auto range = wts->value_range(node);
        this->range_begin = get<0>(range);
        this->range_end = get<1>(range);
        this->is_leaf = wts->is_leaf(node);
    }
    
    void ensure_children()
    {
        if (children.first == nullptr)
        {
            auto children = wts->expand(node);
            this->children = make_pair(make_shared<node_cache>(children[0], wts), make_shared<node_cache>(children[1], wts));
        }
    }
};

// ITERATOR BEGIN
template<class type_csa, class type_wt>
class wild_card_match_iterator : public std::iterator<std::forward_iterator_tag, pair<typename type_csa::size_type, typename type_csa::size_type>>
{
private:
    typedef typename type_wt::node_type  node_type;
    typedef typename type_csa::size_type size_type;
    typedef pair<size_type, size_type>   result_type;

    // (lex_range, node)
    array<stack<pair<range_type,shared_ptr<node_cache<type_csa, type_wt>>> >, 2> lex_ranges;

    type_wt wts;
    size_t a;
    size_t b_idx = -1;
    deque<size_t> b_values;

    size_t s1_size_min_gap;
    size_t s1_size_max_gap;
    size_t s2_size;

    result_type current;

    void split_node(int t)
    {
        auto top = lex_ranges[t].top(); lex_ranges[t].pop();
        auto& node = top.second;
        node->ensure_children();
        auto exp_range = wts.expand(node->node, top.first);
        if (!empty(exp_range[1]))
            lex_ranges[t].emplace(exp_range[1], node->children.second);
        if (!empty(exp_range[0]))
            lex_ranges[t].emplace(exp_range[0], node->children.first);
    };
    
    void adjust_b_range()
    {
        // shrink
        while (!b_values.empty() && a + s1_size_min_gap > b_values.front())
            b_values.pop_front();
        // expand
        while (!lex_ranges[1].empty() && a + s1_size_max_gap >= lex_ranges[1].top().second->range_begin)
        {
            if (lex_ranges[1].top().second->is_leaf)
            {                    
                b_values.push_back(lex_ranges[1].top().second->range_begin);
                lex_ranges[1].pop();
            }
            else
                split_node(1);
        }
    }

    bool next_batch()
    {
        b_idx = 0;
        
        // find next connected match
        while (!lex_ranges[0].empty() 
            && !b_values.empty()
            && lex_ranges[0].top().second->range_begin + s1_size_min_gap <= b_values.back())
        {
            if (lex_ranges[0].top().second->is_leaf)
            {
                a = lex_ranges[0].top().second->range_begin;
                lex_ranges[0].pop();
                adjust_b_range();
                if (!b_values.empty())
                    return true;
            }
            else
                split_node(0);
        }
            
        // find next independent match
        while (!lex_ranges[0].empty() && !lex_ranges[1].empty())
        {
            const auto& top0 = lex_ranges[0].top().second;
            const auto& top1 = lex_ranges[1].top().second;
            if (top0->range_end + s1_size_max_gap < top1->range_begin)
                lex_ranges[0].pop();
            else if (top0->range_begin + s1_size_min_gap > top1->range_end)
                lex_ranges[1].pop();
            else if (top0->is_leaf && top1->is_leaf)
            {
                a = top0->range_begin;
                b_values.push_back(top1->range_begin);
                lex_ranges[0].pop();
                lex_ranges[1].pop();
                adjust_b_range();
                return true;
            }
            else
                split_node(top1->range_size() > top0->range_size() ? 1 : 0);
        }

        b_values.clear();
        return false;
    }

    void next()
    {
        ++b_idx;
        if (!valid() && !next_batch())
            return; // end

        current = make_pair(a, b_values[b_idx]+s2_size-1);
        //cout << current.first << " " << current.second << endl;
    }

public:
    wild_card_match_iterator(type_csa& csa, type_wt& wts, string s1, string s2, size_t min_gap, size_t max_gap)
    {
        this->wts = wts;

        s2_size = s2.size();
        s1_size_min_gap = s1.size() + min_gap;
        s1_size_max_gap = s1.size() + max_gap;

        auto root_node = make_shared<node_cache<type_csa, type_wt>>(wts.root(), &wts);
        size_type sp = 1, ep = 0;
        if (0 != backward_search(csa, 0, csa.size()-1, s1.begin(), s1.end(), sp, ep))
            lex_ranges[0].emplace(range_type(sp, ep),root_node);
        if (0 != backward_search(csa, 0, csa.size()-1, s2.begin(), s2.end(), sp, ep))
            lex_ranges[1].emplace(range_type(sp, ep),root_node);

        next();
    }

    bool valid()
    {
        return b_idx < b_values.size();
    }

    result_type operator*() const
    {
        return current;
    }
    result_type* operator->()
    {
        return &current;
    }

    wild_card_match_iterator& operator++()
    {
        next();
        return *this;
    }
};
// ITERATOR END

template <typename type_csa, typename type_wt>
size_t match_dfs(type_csa& csa, type_wt& wts, string s1, string s2, size_t min_gap, size_t max_gap, std::function<void(typename type_csa::size_type a, typename type_csa::size_type b)> callback)
{
    size_t res = 0;
    auto it = wild_card_match_iterator<type_csa, type_wt>(csa, wts, s1, s2, min_gap, max_gap);
    for (; it.valid(); ++it, ++res)
        callback(it->first, it->second);
    return res;
}

template <typename type_csa, typename type_wt>
size_t match_dfs2(type_csa csa, type_wt wts, string s1, string s2, size_t min_gap, size_t max_gap, std::function<void(typename type_csa::size_type a, typename type_csa::size_type b)> callback)
{
    using node_type = typename type_wt::node_type;
    using size_type = typename type_csa::size_type;

    auto s1_size = s1.size();
    auto s2_size = s2.size();
    auto s1_size_min_gap = s1_size + min_gap;
    auto s1_size_max_gap = s1_size + max_gap;

    size_t cnt_nodes = 0;
    
    array<stack<pair<range_type,size_t>>, 2> lex_ranges; // stores vector of (lex_range,node_id) pairs for each pattern 
    
    // (node, LAZY lchild, LAZY rchild, lbound, rbound)
    vector<std::tuple<node_type, size_t, size_t, size_type, size_type>> nodes;
    
    auto left_bound = [&](int t) { return get<3>(nodes[lex_ranges[t].top().second]); };
    auto right_bound = [&](int t) { return get<4>(nodes[lex_ranges[t].top().second]); };
    auto range_size = [&](int t) {
        auto& node = nodes[lex_ranges[t].top().second];
        return get<4>(node) - get<3>(node) + 1;
    };
        
    auto add_node = [&](node_type node) 
    { 
        cnt_nodes++;
        auto range = wts.value_range(node);
        nodes.emplace_back(node, 0, 0, get<0>(range), get<1>(range)); 
        return nodes.size() - 1; 
    };
    auto split_node = [&](int t) 
    { 
        auto top = lex_ranges[t].top(); lex_ranges[t].pop();
        auto node_id = top.second;
        if (get<1>(nodes[node_id]) == 0) 
        {
            auto children = wts.expand(get<0>(nodes[node_id]));
            auto id0 = add_node(children[0]);
            auto id1 = add_node(children[1]);
            get<1>(nodes[node_id]) = id0; // without those helper variables, this operation would NOT change the LHS!
            get<2>(nodes[node_id]) = id1;
        }
        auto exp_range = wts.expand(get<0>(nodes[node_id]), top.first);
        if (!empty(exp_range[1]))
            lex_ranges[t].emplace(exp_range[1], get<2>(nodes[node_id]));
        if (!empty(exp_range[0]))
            lex_ranges[t].emplace(exp_range[0], get<1>(nodes[node_id]));
    };
    
    size_type sp = 1, ep = 0;
    if (0 == backward_search(csa, 0, csa.size()-1, s1.begin(), s1.end(), sp, ep))
        return 0;
    lex_ranges[0].emplace(range_type(sp, ep),0);
    if (0 == backward_search(csa, 0, csa.size()-1, s2.begin(), s2.end(), sp, ep))
        return 0;
    lex_ranges[1].emplace(range_type(sp, ep),0);
    add_node(wts.root());
        
    size_t result = 0;

    deque<size_t> b_values;
    while (!lex_ranges[0].empty() && !lex_ranges[1].empty())
    {
        if (right_bound(0) + s1_size_max_gap < left_bound(1))
            lex_ranges[0].pop();
        else if (left_bound(0) + s1_size_min_gap > right_bound(1))
            lex_ranges[1].pop();
        // Known: current ranges are non-empty and gap-overlapping => expand/report
        else if (wts.is_leaf(get<0>(nodes[lex_ranges[1].top().second])))
        {
            // KNOWN: expansion expands 0 first
            // => 1 is leaf later 
            // => 0 is also a leaf
            // => both ranges are equal in size (= 1)
                            
            b_values.push_back(left_bound(1));
            lex_ranges[1].pop();
                        
            while (!lex_ranges[0].empty() 
                && !b_values.empty()
                && left_bound(0) + s1_size_min_gap <= b_values.back())
            {
                // next A
                if (wts.is_leaf(get<0>(nodes[lex_ranges[0].top().second])))
                {
                    auto a = left_bound(0);
                    lex_ranges[0].pop(); //cout << "PA: " << a << endl;
                    
                    // shrink B range
                    while (!b_values.empty() && a + s1_size_min_gap > b_values.front())
                        b_values.pop_front();
                    // expand B range
                    while (!lex_ranges[1].empty() && a + s1_size_max_gap >= left_bound(1))
                    {
                        if (wts.is_leaf(get<0>(nodes[lex_ranges[1].top().second])))
                        {                    
                            b_values.push_back(left_bound(1));
                            lex_ranges[1].pop(); //cout << "PB: " << left_bound(1) << endl;
                        }
                        else
                            split_node(1);
                    }
                    // end expand B range
                    
                    // report
                    for (auto it = b_values.begin(); it != b_values.end(); ++it, ++result)
                        callback(a, *it+s2_size-1);
                }
                else
                    split_node(0);
            }
        }
        else
            split_node(range_size(1) > range_size(0) ? 1 : 0);
    }
    
    //cerr << "Processed nodes: " << cnt_nodes << " (" <<wts.size() << ")" << endl;
    return result;
}

int main(int argc, char* argv[])
{
    if ( argc < 2 ){
        cout << "Usage: ./" << argv[0] << " file_to_index" << endl;
        cout << "    Input pat1 pat2 min_gap max_gap" << endl;
        return 1;
    }
    csa_wt<wt_huff<rrr_vector<63>>> csa;

    cache_config cc(false,".","WILD_CARD_MATCH_TMP");
    construct(csa, argv[1], cc, 1);

    wt_int<bit_vector, rank_support_v5<>, select_support_scan<1>, select_support_scan<0>> wts;
    
    construct(wts, cache_file_name(conf::KEY_SA, cc));

    util::delete_all_files(cc.file_map);

    cout<<"wts.size()="<<wts.size()<<endl;
    if ( wts.size() < 100 ){
        cout<<"wts="<<wts<<endl;
    }
    
    // CALLBACKS
    using size_type = decltype(csa)::size_type;
    std::function<void(size_type a, size_type b)> callback_cout = [&](size_type a, size_type b) 
    { 
        if (b - a > 100)
            cout << "\t" << a << " " << b << endl; 
        else
            cout << "\t" << extract(csa, a, b) << endl; 
    };
    std::function<void(size_type a, size_type b)> callback_dbg = [&](size_type a, size_type b) 
    { 
        cout << "\t" << a << " " << b << endl; 
    };
    std::function<void(size_type a, size_type b)> callback_nop = [](size_type a, size_type b) { (void)a; (void)b; };

    bool test_and_bench = true;
    if (test_and_bench)
    {
        // TESTS
        auto test = [&](string s1, string s2, size_t min_gap, size_t max_gap) 
        { 
            size_t matches = match_dfs(csa, wts, s1, s2, min_gap, max_gap, callback_nop);
            size_t matches_ref = match_ref(csa, wts, s1, s2, min_gap, max_gap, callback_nop);
            
            bool success = matches == matches_ref;
            if (!success)
            {
                cout << "Error on: s1=" << s1 << ", s2=" << s2 << ", min_gap=" << min_gap << ", max_gap=" << max_gap << endl;
                cout << "MATCHES:     " << matches << endl;
                cout << "MATCHES_ref: " << matches_ref << endl;
            }
            
            return success;
        };
        vector<pair<string, string>> tcs;
        tcs.emplace_back("8", "8");
        tcs.emplace_back("8", "0");
        tcs.emplace_back("a", "1");
        tcs.emplace_back(" ", "8");
        int max = 100;
        for (unsigned int tci = 0; tci < tcs.size(); tci++)
        {
            auto s1 = tcs[tci].first;
            auto s2 = tcs[tci].second;
            cout << "TEST CASE: " << s1 << " " << s2 << endl;
            for (int i = 0; i <= max; i += (rand() % 2) + 1)
            { 
                if (!test(s1, s2, i, max)) goto err;
                if (!test(s1, s2, 0, i)) goto err;
                if (!test(s1, s2, i, i)) goto err;
                if (!test(s1, s2, i, 2*i)) goto err;
                if (!test(s1, s2, 10 * i, 10 * i + 3)) goto err;
                if (!test(s1, s2, 10 * i, 10 * i + 10)) goto err;
            }
        }
        
        // BENCH
        {
            size_t found = 0;
            auto start = timer::now();
            for (unsigned int tci = 0; tci < tcs.size(); tci++)
            {
                auto s1 = tcs[tci].first;
                auto s2 = tcs[tci].second;
                for (int i = 0; i <= max; i += 3)
                { 
                    found += match_dfs(csa, wts, s1, s2, i, max, callback_nop);
                    found += match_dfs(csa, wts, s1, s2, 0, i, callback_nop);
                    found += match_dfs(csa, wts, s1, s2, i, i, callback_nop);
                    found += match_dfs(csa, wts, s1, s2, i, 2*i, callback_nop);
                    found += match_dfs(csa, wts, s1, s2, 10 * i, 10 * i + 3, callback_nop);
                    found += match_dfs(csa, wts, s1, s2, 10 * i, 10 * i + 10, callback_nop);
                }
            }
            auto stop = timer::now();
            cout << "BENCH_DFS: " << duration_cast<milliseconds>(stop-start).count() << "ms for " << found << " occurrences" << endl;
        
            found = 0;
            start = timer::now();
            for (unsigned int tci = 0; tci < tcs.size(); tci++)
            {
                auto s1 = tcs[tci].first;
                auto s2 = tcs[tci].second;
                for (int i = 0; i <= max; i += 3)
                { 
                    found += match_ref(csa, wts, s1, s2, i, max, callback_nop);
                    found += match_ref(csa, wts, s1, s2, 0, i, callback_nop);
                    found += match_ref(csa, wts, s1, s2, i, i, callback_nop);
                    found += match_ref(csa, wts, s1, s2, i, 2*i, callback_nop);
                    found += match_ref(csa, wts, s1, s2, 10 * i, 10 * i + 3, callback_nop);
                    found += match_ref(csa, wts, s1, s2, 10 * i, 10 * i + 10, callback_nop);
                }
            }
            stop = timer::now();
            cout << "BENCH_REF: " << duration_cast<milliseconds>(stop-start).count() << "ms for " << found << " occurrences" << endl;
        }
    }

    // PROMPT
    {
        size_t min_gap = 0;
        size_t max_gap = 1;
        string s1, s2;
        cout << "PROMPT: " << endl;
        while (cin >> s1 >> s2 >> min_gap >> max_gap)
            cout << match_dfs(csa, wts, s1, s2, min_gap, max_gap, callback_cout) << " matches found" << endl;
    }
    err:;
}

