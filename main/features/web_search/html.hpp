#ifndef BF2FE0DF_3087_4ABC_9C3A_8F8EEA0B2EFA
#define BF2FE0DF_3087_4ABC_9C3A_8F8EEA0B2EFA
#ifndef HTML_H
#define HTML_H

#include <memory>
#include <functional>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <cctype>
#include <algorithm>
#include <map>
#include <utility>
#include <iterator>

/**
 * ## Usage

### Access nodes
```cpp
html::parser p;
html::node_ptr node = p.parse(R"(<!DOCTYPE html><body><div attr="val">text</div><!--comment--></body>)");

// `parse` method returns root node of type html::node_t::none
assert(node->type_node == html::node_t::none);
assert(node->at(0)->type_node == html::node_t::doctype);
assert(node->at(1)->type_node == html::node_t::tag);
assert(node->at(1)->at(0)->at(0)->type_node == html::node_t::text);
assert(node->at(1)->at(1)->type_node == html::node_t::comment);

std::cout << "Number of child elements: " << node->size() << std::endl << std::endl; // 2

std::cout << "Loop through child nodes: " << std::endl;
for(auto& n : *(node->at(1))) {
	std::cout << n->to_html() << std::endl;
}
std::cout << std::endl;

std::cout << "Get node properties: " << std::endl;
std::cout << "DOCTYPE name: " << node->at(0)->content << std::endl; // html
std::cout << "BODY tag: " << node->at(1)->tag_name << std::endl; // body
std::cout << "Attr value: " << node->at(1)->at(0)->get_attr("attr") << std::endl; // val
std::cout << "Text node: " << node->at(1)->at(0)->at(0)->content << std::endl; // text
std::cout << "Comment: " << node->at(1)->at(1)->content << std::endl; // comment
```

### Find nodes using `select` method
[List of available selectors](#selectors)
```cpp
html::parser p;
html::node_ptr node = p.parse(R"(<div id="my_id"><p class="my_class"></p></div>)");
std::vector<html::node*> selected = node->select("div#my_id p.my_class");
for(auto elem : selected) {
	std::cout << elem->to_html() << std::endl;
}
```

### Access nodes using callback (called when the document is parsed)
```cpp
html::parser p;
p.set_callback("meta[http-equiv='Content-Type'][content*='charset=']", [](html::node& n) {
	if (n.type_node == html::node_t::tag && n.type_tag == html::tag_t::open) {
		std::cout << "Callback with selector to filter elements:" << std::endl;
		std::cout << n.to_html() << std::endl << std::endl;
	}
});
p.set_callback([](html::node& n) {
	if(n.type_node == html::node_t::tag && n.type_tag == html::tag_t::open && n.tag_name == "meta") {
		if(n.get_attr("http-equiv") == "Content-Type" && n.get_attr("content").find("charset=") != std::string::npos) {
			std::cout << "Callback without selector:" << std::endl;
			std::cout << n.to_html() << std::endl;
		}
	}
});
p.parse(R"(<head><title>Title</title><meta http-equiv="Content-Type" content="text/html; charset=utf-8" /></head>)");
```

### Manual search
```cpp
std::cout << "Search `li` tags which not in `ol`:" << std::endl;
html::parser p;
html::node_ptr node = p.parse("<ul><li>li1</li><li>li2</li></ul><ol><li>li</li></ol>");
node->walk([](html::node& n) {
	if(n.type_node == html::node_t::tag && n.tag_name == "ol") {
		return false; // not scan child tags
	}
	if(n.type_node == html::node_t::tag && n.tag_name == "li") {
		std::cout << n.to_html() << std::endl;
	}
	return true; // scan child tags
});
```

### Finding unclosed tags
```cpp
html::parser p;

// Callback to handle errors
p.set_callback([](html::err_t e, html::node& n) {
	if(e == html::err_t::tag_not_closed) {
		std::cout << "Tag not closed: " << n.to_html(' ', false);
		std::string msg;
		html::node* current = &n;
		while(current->get_parent()) {
			msg.insert(0, " " + current->tag_name);
			current = current->get_parent();
		}
		msg.insert(0, "\nPath:");
		std::cout << msg << std::endl;
	}
});
p.parse("<div><p><a></p></div>");
```

### Print document formatted
```cpp
html::parser p;
html::node_ptr node = p.parse("<ul><li>li1</li><li>li2</li></ul><ol><li>li</li></ol>");

// method takes two arguments, the indentation character and whether to output child elements (tabulation and true by default)
std::cout << node->to_html(' ', true) << std::endl;
```

### Print text content of a node
```cpp
html::parser p;
html::node_ptr node = p.parse("<div><p><b>First</b> p</p><p><i>Second</i> p</p>Text<br />Text</div>");

std::cout << "Print text with line breaks preserved:" << std::endl;
std::cout << node->to_text() << std::endl << std::endl;

std::cout << "Print text with line breaks replaced with spaces:" << std::endl;
std::cout << node->to_text(true) << std::endl;
```

### Build document
```cpp
std::cout << "Using helpers:" << std::endl;

html::node hdiv = html::utils::make_node(html::node_t::tag, "div");
hdiv.append(html::utils::make_node(html::node_t::text, "Link:"));
hdiv.append(html::utils::make_node(html::node_t::tag, "br"));
html::node ha = html::utils::make_node(html::node_t::tag, "a", {{"href", "https://github.com/"}, {"class", "a_class"}});
ha.append(html::utils::make_node(html::node_t::text, "Github.com"));
std::cout << hdiv.append(ha).to_html() << std::endl << std::endl;

std::cout << "Without helpers:" << std::endl;

html::node div;
div.type_node = html::node_t::tag;
div.tag_name = "div";

html::node text;
text.type_node = html::node_t::text;
text.content = "Link:";
div.append(text);

html::node br;
br.type_node = html::node_t::tag;
br.tag_name = "br";
br.self_closing = true;
div.append(br);

html::node a;
a.type_node = html::node_t::tag;
a.tag_name = "a";
a.set_attr("href", "https://github.com/");
a.set_attr("class", "a_class");

html::node a_text;
a_text.type_node = html::node_t::text;
a_text.content = "Github.com";
a.append(a_text);

div.append(a);

std::cout << div.to_html() << std::endl;
```

## Selectors
| Selector example | Description | select | callback |
|-|-|-|-|
| * | all elements | √ | √ |
| div | tag name | √ | √ |
| #id1 | id="id1" | √ | √ |
| .class1 | class="class1" | √ | √ |
| .class1.class2 | class="class1 class2" | √ | √ |
| :first | first element | √ | √ |
| :last | last element | √ | - |
| :eq(3) | element index = 3 (starts from 0) | √ | √ |
| :gt(3) | element index > 3 (starts from 0) | √ | √ |
| :lt(3) | element index < 3 (starts from 0) | √ | √ |
| [attr] | element that have attribute "attr" | √ | √ |
| [attr='val'] | attribute is equal to "val" | √ | √ |
| [attr!='val'] | attribute is not equal to "val" or does not exist | √ | √ |
| [attr^='http:'] | attribute starts with "http:" | √ | √ |
| [attr$='.jpeg'] | attribute ends with ".jpeg" | √ | √ |
| [attr*='/path/'] | attribute contains "/path/" | √ | √ |
| [attr~='flower'] | attribute contains word "flower" | √ | √ |
| [attr&vert;='en'] | attribute equal to "en" or starting with "en-" | √ | √ |
| div#id1.class1[attr='val'] | element that matches all of these selectors | √ | √ |
| p,div | element that matches any of these selectors | √ | √ |
| div p | all `<p>` elements inside `<div>` elements | √ | - |
| div>p | all `<p>` elements where the parent is a `<div>` element | √ | - |
| div div>p>i | combination of nested selectors  | √ | - | 
 * 
 * 
 */

namespace html {

	class selector;
	class parser;
	class node;

	using node_ptr = std::unique_ptr<node>;

	enum class node_t {
		none,
		text,
		tag,
		comment,
		doctype
	};

	enum class tag_t {
		none,
		open,
		close,
	};

	enum class err_t {
		tag_not_closed
	};

	class node {
	public:
		node(node* parent = nullptr) : parent(parent) {}
		node(const node&);
		node(node&& d) noexcept
		: type_node(d.type_node)
		, type_tag(d.type_tag)
		, self_closing(d.self_closing)
		, tag_name(std::move(d.tag_name))
		, content(std::move(d.content))
		, parent(nullptr)
		, bogus_comment(d.bogus_comment)
		, children(std::move(d.children))
		, attributes(std::move(d.attributes))
		, index(0)
		, node_count(d.node_count) {}
		node* at(size_t i) const {
			if(i < children.size()) {
				return children[i].get();
			}
			return nullptr;
		}
		size_t size() const {
			return children.size();
		}
		bool empty() const {
			return children.empty();
		}
		std::vector<node_ptr>::iterator begin() {
			return children.begin();
		}
		std::vector<node_ptr>::iterator end() {
			return children.end();
		}
		std::vector<node_ptr>::const_iterator begin() const {
			return children.begin();
		}
		std::vector<node_ptr>::const_iterator end() const {
			return children.end();
		}
		std::vector<node_ptr>::const_iterator cbegin() const {
			return children.cbegin();
		}
		std::vector<node_ptr>::const_iterator cend() const {
			return children.cend();
		}
		std::vector<node*> select(const selector, bool nested = true);
		std::string to_html(char indent = '	', bool child = true, bool text = true) const;
		std::string to_raw_html(bool child = true, bool text = true) const;
		std::string to_text(bool raw = false) const;
		node* get_parent() const {
			return parent;
		}
		bool has_attr(const std::string&) const;
		std::string get_attr(const std::string&) const;
		void set_attr(const std::string&, const std::string&);
		void set_attr(const std::map<std::string, std::string>& attributes);
		void del_attr(const std::string&);
		node& append(const node&);
		void walk(std::function<bool(node&)>);
		node_t type_node = node_t::none;
		tag_t type_tag = tag_t::none;
		bool self_closing = false;
		std::string tag_name;
		std::string content;
	private:
		node* parent = nullptr;
		bool bogus_comment = false;
		std::vector<node_ptr> children;
		std::map<std::string, std::string> attributes;
		int index = 0;
		int node_count = 0;
		void copy(const node*, node*);
		void walk(node&, std::function<bool(node&)>);
		void to_html(std::ostream&, bool, bool, int, int&, char, bool&, bool&) const;
		void to_raw_html(std::ostream&, bool, bool) const;
		void to_text(std::ostream&, bool&) const;
		friend class selector;
		friend class parser;
	};

	class selector {
	public:
		selector() = default;
		selector(const std::string&);
		selector(const char* s) : selector(std::string(s)) {}
		operator bool() const {
			return !matchers.empty();
		}
	private:
		struct condition {
			condition() = default;
			condition(const condition& d) = default;
			condition(condition&&) noexcept;
			std::string tag_name;
			std::string id;
			std::string class_name;
			std::string index = "0";
			std::string attr;
			std::string attr_value;
			std::string attr_operator;
			bool operator()(const node&) const;
		};
		struct selector_matcher {
			selector_matcher() = default;
			selector_matcher(const selector_matcher&) = default;
			selector_matcher(selector_matcher&&) noexcept;
			bool operator()(const node&) const;
			bool dc_first = false;
			bool dc_second = false;
		private:
			bool all_match = false;
			std::vector<std::vector<condition>> conditions;
			friend class selector;
		};
		std::vector<selector_matcher>::const_iterator begin() const {
			return matchers.begin();
		}
		std::vector<selector_matcher>::const_iterator end() const {
			return matchers.end();
		}
		std::vector<selector_matcher> matchers;
		enum class state_t {
			route, tag, st_class, id, st_operator, index, attr, attr_operator, attr_val
		};
		bool is_state_route(char c) {
			return c == 0 || c == ' ' || c == '[' || c == ':' || c == '.' || c == '#' || c == ',' || c == '>';
		}
		friend class node;
		friend class parser;
	};

	class parser {
	public:
		parser& set_callback(std::function<void(node&)> cb);
		parser& set_callback(const selector, std::function<void(node&)> cb);
		parser& set_callback(std::function<void(err_t, node&)> cb);
		void clear_callbacks();
		node_ptr parse(const std::string&);
		node_ptr parse(std::istream&);
		template<class InputIt>
		node_ptr parse(InputIt, InputIt);
	private:
		void operator()(node&);
		void handle_node();
		node* current_ptr = nullptr;
		node_ptr new_node;
		std::vector<std::pair<selector, std::function<void(node&)>>> callback_node;
		std::vector<std::function<void(err_t, node&)>> callback_err;
		enum class state_t {
			data, rawtext, tag_open, end_tag_open, tag_name, rawtext_less_than_sign, rawtext_end_tag_open, rawtext_end_tag_name, 
			before_attribute_name, attribute_name, after_attribute_name, before_attribute_value, attribute_value_double, 
			attribute_value_single, attribute_value_unquoted, after_attribute_value_quoted, self_closing, bogus_comment, 
			markup_dec_open_state, comment_start, comment_start_dash, comment, comment_end_dash, comment_end, 
			before_doctype_name, doctype_name
		} state;
	};

	namespace utils {

		node make_node(node_t, const std::string&, const std::map<std::string, std::string>& attributes = {});
		template <class T, class... Args>
		typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type
			make_unique(Args &&...args) {
			return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
		}
		template <class T>
		typename std::enable_if<std::is_array<T>::value, std::unique_ptr<T>>::type
			make_unique(std::size_t n) {
			typedef typename std::remove_extent<T>::type RT;
			return std::unique_ptr<T>(new RT[n]);

		}
		bool contains_word(const std::string&, const std::string&);
		template<class InputIt>
		bool ilook_ahead(InputIt, InputIt, const std::string&);
		std::string replace_any_copy(const std::string&, const std::string&, const std::string&);
		inline bool is_uppercase_alpha(char c) {
			return 'A' <= c && c <= 'Z';
		}
		inline bool is_lowercase_alpha(char c) {
			return 'a' <= c && c <= 'z';
		}
		inline bool is_alpha(char c) {
			return is_uppercase_alpha(c) || is_lowercase_alpha(c);
		}
		inline bool is_digit(char c) {
			return '0' <= c && c <= '9';
		}
		inline bool is_space(char c) {
			return c == 0x09 || c == 0x0A || c == 0x0C || c == 0x20 || c == 0x0D;
		}

	}

}

#endif


#endif /* BF2FE0DF_3087_4ABC_9C3A_8F8EEA0B2EFA */
