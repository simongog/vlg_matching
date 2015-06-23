#pragma once

#include "utils.hpp"
#include "collection.hpp"
#include "sdsl/int_vector.hpp"

#include <regex>

class index_regexp
{
    public:
        typedef sdsl::int_vector<0>::size_type size_type;
        typedef sdsl::int_vector<0>::value_type value_type;
        typedef std::string text_type;
        std::string name() const
        {
            std::string index_name = IDXNAME;
            return "REGEXP-"+index_name;
        }
    protected:
        text_type m_text;
    public:
        index_regexp(collection& col)
        {
            sdsl::int_vector_mapper<0> sdsl_text(col.file_map[consts::KEY_TEXT]);
            std::copy(sdsl_text.begin(),sdsl_text.end(),std::back_inserter(m_text));
        }

        size_type serialize(std::ostream&, sdsl::structure_tree_node* v=NULL, std::string name="")const
        {
            sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
            sdsl::structure_tree::add_size(child, 0);
            return 0;
        }

        void load(std::istream&)
        {
        }

        void swap(index_regexp& ir)
        {
            if (this != &ir) {
                m_text.swap(ir.m_text);
            }
        }

        //! Search for the k documents which contain the search term most frequent
        gapped_search_result
        search(const gapped_pattern& pat) const
        {
            /* (1) construct regexp */
            std::regex rx(pat.raw_regexp.begin(),pat.raw_regexp.end(),REGEXP_TYPE);

            /* (2) find all matching pos */
            auto matches_begin = std::sregex_iterator(m_text.begin(),m_text.end(),rx);
            auto matches_end = std::sregex_iterator();


            /* (3) copy the output */
            gapped_search_result res;
            for (std::sregex_iterator it = matches_begin; it != matches_end; ++it) {
                res.positions.push_back(it->position());
            }
            return res;
        }
};