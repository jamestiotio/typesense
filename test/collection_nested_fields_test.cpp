#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <collection_manager.h>
#include "collection.h"

class CollectionNestedFieldsTest : public ::testing::Test {
protected:
    Store* store;
    CollectionManager& collectionManager = CollectionManager::get_instance();
    std::atomic<bool> quit = false;

    std::vector <std::string> query_fields;
    std::vector <sort_by> sort_fields;

    void setupCollection() {
        std::string state_dir_path = "/tmp/typesense_test/collection_nested";
        LOG(INFO) << "Truncating and creating: " << state_dir_path;
        system(("rm -rf " + state_dir_path + " && mkdir -p " + state_dir_path).c_str());

        store = new Store(state_dir_path);
        collectionManager.init(store, 1.0, "auth_key", quit);
        collectionManager.load(8, 1000);
    }

    virtual void SetUp() {
        setupCollection();
    }

    virtual void TearDown() {
        collectionManager.dispose();
        delete store;
    }
};

TEST_F(CollectionNestedFieldsTest, FlattenJSONObject) {
    auto json_str = R"({
        "company": {"name": "nike"},
        "employees": { "num": 1200 },
        "locations": [
            { "pincode": 100, "country": "USA",
              "address": { "street": "One Bowerman Drive", "city": "Beaverton", "products": ["shoes", "tshirts"] }
            },
            { "pincode": 200, "country": "Canada",
              "address": { "street": "175 Commerce Valley", "city": "Thornhill", "products": ["sneakers", "shoes"] }
            }
        ]}
    )";

    std::vector<field> nested_fields = {
        field("locations", field_types::OBJECT_ARRAY, false)
    };

    // array of objects
    std::vector<field> flattened_fields;
    nlohmann::json doc = nlohmann::json::parse(json_str);
    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());
    ASSERT_EQ(5, flattened_fields.size());

    for(const auto& f: flattened_fields) {
        ASSERT_TRUE(f.is_array());
    }

    auto expected_json = R"(
        {
            ".flat": ["locations.address.city","locations.address.products","locations.address.street",
                      "locations.country", "locations.pincode"],
            "company":{"name":"nike"},
            "employees":{"num":1200},
            "locations":[
                {"address":{"city":"Beaverton","products":["shoes","tshirts"],
                "street":"One Bowerman Drive"},"country":"USA","pincode":100},

                {"address":{"city":"Thornhill","products":["sneakers","shoes"],
                "street":"175 Commerce Valley"},"country":"Canada","pincode":200}
            ],

            "locations.address.city":["Beaverton","Thornhill"],
            "locations.address.products":["shoes","tshirts","sneakers","shoes"],
            "locations.address.street":["One Bowerman Drive","175 Commerce Valley"],
            "locations.country":["USA","Canada"],
            "locations.pincode":[100,200]
        }
    )";

    // handle order of generation differences between compilers (due to iteration of unordered map)
    auto expected_flat_fields = doc[".flat"].get<std::vector<std::string>>();
    std::sort(expected_flat_fields.begin(), expected_flat_fields.end());
    doc[".flat"] = expected_flat_fields;

    ASSERT_EQ(doc.dump(), nlohmann::json::parse(expected_json).dump());

    // plain object
    flattened_fields.clear();
    doc = nlohmann::json::parse(json_str);
    nested_fields = {
        field("company", field_types::OBJECT, false)
    };

    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());

    expected_json = R"(
        {
          ".flat": ["company.name"],
          "company":{"name":"nike"},
          "company.name":"nike",
          "employees":{"num":1200},
          "company.name":"nike",
          "locations":[
                {"address":{"city":"Beaverton","products":["shoes","tshirts"],
                 "street":"One Bowerman Drive"},"country":"USA","pincode":100},
                {"address":{"city":"Thornhill","products":["sneakers","shoes"],"street":"175 Commerce Valley"},
                 "country":"Canada","pincode":200}
          ]
        }
    )";

    ASSERT_EQ(doc.dump(), nlohmann::json::parse(expected_json).dump());

    // plain object inside an array
    flattened_fields.clear();
    doc = nlohmann::json::parse(json_str);
    nested_fields = {
        field("locations.address", field_types::OBJECT, false)
    };

    ASSERT_FALSE(field::flatten_doc(doc, nested_fields, flattened_fields).ok()); // must be of type object_array

    nested_fields = {
        field("locations.address", field_types::OBJECT_ARRAY, false)
    };

    flattened_fields.clear();
    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());

    expected_json = R"(
        {
          ".flat": ["locations.address.city","locations.address.products","locations.address.street"],
          "company":{"name":"nike"},
          "employees":{"num":1200},
          "locations":[
                {"address":{"city":"Beaverton","products":["shoes","tshirts"],
                 "street":"One Bowerman Drive"},"country":"USA","pincode":100},
                {"address":{"city":"Thornhill","products":["sneakers","shoes"],"street":"175 Commerce Valley"},
                 "country":"Canada","pincode":200}
          ],
          "locations.address.city":["Beaverton","Thornhill"],
          "locations.address.products":["shoes","tshirts","sneakers","shoes"],
          "locations.address.street":["One Bowerman Drive","175 Commerce Valley"]
        }
    )";

    // handle order of generation differences between compilers (due to iteration of unordered map)
    expected_flat_fields = doc[".flat"].get<std::vector<std::string>>();
    std::sort(expected_flat_fields.begin(), expected_flat_fields.end());
    doc[".flat"] = expected_flat_fields;
    ASSERT_EQ(doc.dump(), nlohmann::json::parse(expected_json).dump());

    // primitive inside nested object
    flattened_fields.clear();
    doc = nlohmann::json::parse(json_str);
    nested_fields = {
        field("company.name", field_types::STRING, false)
    };

    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());

    expected_json = R"(
        {
          ".flat": ["company.name"],
          "company":{"name":"nike"},
          "company.name":"nike",
          "employees":{"num":1200},
          "locations":[
                {"address":{"city":"Beaverton","products":["shoes","tshirts"],
                 "street":"One Bowerman Drive"},"country":"USA","pincode":100},
                {"address":{"city":"Thornhill","products":["sneakers","shoes"],"street":"175 Commerce Valley"},
                 "country":"Canada","pincode":200}
          ]
        }
    )";

    ASSERT_EQ(doc.dump(), nlohmann::json::parse(expected_json).dump());
}

TEST_F(CollectionNestedFieldsTest, TestNestedArrayField) {
    auto json_str = R"({
        "company": {"name": "nike"},
        "employees": {
            "num": 1200,
            "detail": {
                "num_tags": 2,
                "tags": ["plumber", "electrician"]
            },
            "details": [{
                "num_tags": 2,
                "tags": ["plumber", "electrician"]
            }]
        },
        "locations": [
            { "pincode": 100, "country": "USA",
              "address": { "street": "One Bowerman Drive", "city": "Beaverton", "products": ["shoes", "tshirts"] }
            },
            { "pincode": 200, "country": "Canada",
              "address": { "street": "175 Commerce Valley", "city": "Thornhill", "products": ["sneakers", "shoes"] }
            }
        ]}
    )";

    std::vector<field> nested_fields = {
        field("locations", field_types::OBJECT_ARRAY, false)
    };

    // array of objects
    std::vector<field> flattened_fields;
    nlohmann::json doc = nlohmann::json::parse(json_str);
    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());
    ASSERT_EQ(5, flattened_fields.size());

    for(const auto& f: flattened_fields) {
        ASSERT_TRUE(f.is_array());
        ASSERT_TRUE(f.nested_array);
    }

    flattened_fields.clear();

    // test against whole object

    nested_fields = {
        field("employees", field_types::OBJECT, false)
    };

    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());
    ASSERT_EQ(5, flattened_fields.size());

    for(const auto& f: flattened_fields) {
        if(StringUtils::begins_with(f.name, "employees.details")) {
            ASSERT_TRUE(f.nested_array);
        } else {
            ASSERT_FALSE(f.nested_array);
        }
    }

    // test against deep paths
    flattened_fields.clear();
    doc = nlohmann::json::parse(json_str);
    nested_fields = {
        field("employees.details.num_tags", field_types::INT32_ARRAY, false),
        field("employees.details.tags", field_types::STRING_ARRAY, false),
        field("employees.detail.tags", field_types::STRING_ARRAY, false),
    };

    ASSERT_TRUE(field::flatten_doc(doc, nested_fields, flattened_fields).ok());
    ASSERT_EQ(3, flattened_fields.size());

    ASSERT_EQ("employees.detail.tags",flattened_fields[0].name);
    ASSERT_FALSE(flattened_fields[0].nested_array);

    ASSERT_EQ("employees.details.tags",flattened_fields[1].name);
    ASSERT_TRUE(flattened_fields[1].nested_array);

    ASSERT_EQ("employees.details.num_tags",flattened_fields[2].name);
    ASSERT_TRUE(flattened_fields[2].nested_array);
}

TEST_F(CollectionNestedFieldsTest, FlattenJSONObjectHandleErrors) {
    auto json_str = R"({
        "company": {"name": "nike"},
        "employees": { "num": 1200 }
    })";

    std::vector<field> nested_fields = {
        field("locations", field_types::OBJECT_ARRAY, false)
    };
    std::vector<field> flattened_fields;

    nlohmann::json doc = nlohmann::json::parse(json_str);
    auto flatten_op = field::flatten_doc(doc, nested_fields, flattened_fields);
    ASSERT_FALSE(flatten_op.ok());
    ASSERT_EQ("Field `locations` was not found or has an incorrect type.", flatten_op.error());

    nested_fields = {
        field("company", field_types::INT32, false)
    };

    flattened_fields.clear();
    flatten_op = field::flatten_doc(doc, nested_fields, flattened_fields);
    ASSERT_FALSE(flatten_op.ok());
    ASSERT_EQ("Field `company` was not found or has an incorrect type.", flatten_op.error());
}

TEST_F(CollectionNestedFieldsTest, SearchOnFieldsOnWildcardSchema) {
    std::vector<field> fields = {field(".*", field_types::AUTO, false, true)};

    auto op = collectionManager.create_collection("coll1", 1, fields, "", 0, field_types::AUTO, {}, {}, true);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc = R"({
        "id": "0",
        "company": {"name": "Nike Inc."},
        "employees": {
            "num": 1200,
            "tags": ["senior plumber", "electrician"]
        },
        "locations": [
            { "pincode": 100, "country": "USA",
              "address": { "street": "One Bowerman Drive", "city": "Beaverton", "products": ["shoes", "tshirts"] }
            },
            { "pincode": 200, "country": "Canada",
              "address": { "street": "175 Commerce Valley", "city": "Thornhill", "products": ["sneakers", "shoes"] }
            }
        ]
    })"_json;

    auto add_op = coll1->add(doc.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());
    nlohmann::json create_res = add_op.get();
    ASSERT_EQ(doc.dump(), create_res.dump());

    // search both simply nested and deeply nested array-of-objects
    auto results = coll1->search("electrician commerce", {"employees", "locations"}, "", {}, sort_fields,
                                 {0}, 10, 1, FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["hits"].size());
    ASSERT_EQ(doc, results["hits"][0]["document"]);

    auto highlight_doc = R"({
      "employees":{
        "tags":[
          "senior plumber",
          "<mark>electrician</mark>"
        ]
      },
      "locations":[
        {
          "address":{
            "street":"One Bowerman Drive"
          }
        },
        {
          "address":{
            "street":"175 <mark>Commerce</mark> Valley"
          }
        }
      ]
    })"_json;

    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());
    ASSERT_EQ(0, results["hits"][0]["highlights"].size());

    // search specific nested fields, only matching field is highlighted by default
    results = coll1->search("one shoe", {"locations.address.street", "employees.tags"}, "", {}, sort_fields,
                            {0}, 10, 1, FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["hits"].size());
    ASSERT_EQ(doc, results["hits"][0]["document"]);

    highlight_doc = R"({
      "locations":[
        {
          "address":{
            "street":"<mark>One</mark> Bowerman Drive"
          }
        },
        {
          "address":{
            "street":"175 Commerce Valley"
          }
        }
      ]
    })"_json;

    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());
    ASSERT_EQ(0, results["hits"][0]["highlights"].size());

    // try to search nested fields that don't exist
    auto res_op = coll1->search("one shoe", {"locations.address.str"}, "", {}, sort_fields,
                                {0}, 10, 1, FREQUENCY, {true});
    ASSERT_FALSE(res_op.ok());
    ASSERT_EQ("Could not find a field named `locations.address.str` in the schema.", res_op.error());

    res_op = coll1->search("one shoe", {"locations.address.foo"}, "", {}, sort_fields,
                           {0}, 10, 1, FREQUENCY, {true});
    ASSERT_FALSE(res_op.ok());
    ASSERT_EQ("Could not find a field named `locations.address.foo` in the schema.", res_op.error());

    res_op = coll1->search("one shoe", {"locations.foo.street"}, "", {}, sort_fields,
                           {0}, 10, 1, FREQUENCY, {true});
    ASSERT_FALSE(res_op.ok());
    ASSERT_EQ("Could not find a field named `locations.foo.street` in the schema.", res_op.error());
}

TEST_F(CollectionNestedFieldsTest, IncludeExcludeFields) {
    auto doc_str = R"({
        "company": {"name": "Nike Inc."},
        "employees": {
            "num": 1200,
            "tags": ["senior plumber", "electrician"]
        },
        "employee": true,
        "locations": [
            { "pincode": 100, "country": "USA",
              "address": { "street": "One Bowerman Drive", "city": "Beaverton", "products": ["shoes", "tshirts"] }
            },
            { "pincode": 200, "country": "Canada",
              "address": { "street": "175 Commerce Valley", "city": "Thornhill", "products": ["sneakers", "shoes"] }
            }
        ],
        "one_obj_arr": [{"foo": "bar"}]
    })";

    auto doc = nlohmann::json::parse(doc_str);

    Collection::prune_doc(doc, tsl::htrie_set<char>(), {"one_obj_arr.foo"});
    ASSERT_EQ(0, doc.count("one_obj_arr"));

    // handle non-existing exclude field
    doc = nlohmann::json::parse(doc_str);
    Collection::prune_doc(doc, {"employees.num", "employees.tags"}, {"foobar"});
    ASSERT_EQ(1, doc.size());
    ASSERT_EQ(1, doc.count("employees"));
    ASSERT_EQ(2, doc["employees"].size());

    // select a specific field within nested array object
    doc = nlohmann::json::parse(doc_str);
    Collection::prune_doc(doc, {"locations.address.city"}, tsl::htrie_set<char>());
    ASSERT_EQ(R"({"locations":[{"address":{"city":"Beaverton"}},{"address":{"city":"Thornhill"}}]})", doc.dump());

    // select 2 fields within nested array object
    doc = nlohmann::json::parse(doc_str);
    Collection::prune_doc(doc, {"locations.address.city", "locations.address.products"}, tsl::htrie_set<char>());
    ASSERT_EQ(R"({"locations":[{"address":{"city":"Beaverton","products":["shoes","tshirts"]}},{"address":{"city":"Thornhill","products":["sneakers","shoes"]}}]})", doc.dump());

    // exclusion takes preference
    doc = nlohmann::json::parse(doc_str);
    Collection::prune_doc(doc, {"locations.address.city"}, {"locations.address.city"});
    ASSERT_EQ(R"({})", doc.dump());

    // include object, exclude sub-fields
    doc = nlohmann::json::parse(doc_str);
    Collection::prune_doc(doc, {"locations.address.city", "locations.address.products"}, {"locations.address.city"});
    ASSERT_EQ(R"({"locations":[{"address":{"products":["shoes","tshirts"]}},{"address":{"products":["sneakers","shoes"]}}]})", doc.dump());
}

TEST_F(CollectionNestedFieldsTest, HighlightNestedFieldFully) {
    std::vector<field> fields = {field(".*", field_types::AUTO, false, true)};

    auto op = collectionManager.create_collection("coll1", 1, fields, "", 0, field_types::AUTO, {}, {}, true);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc = R"({
        "company_names": ["Space Corp. LLC", "Drive One Inc."],
        "company": {"names": ["Space Corp. LLC", "Drive One Inc."]},
        "locations": [
            { "pincode": 100, "country": "USA",
              "address": { "street": "One Bowerman Drive", "city": "Beaverton", "products": ["shoes", "tshirts"] }
            },
            { "pincode": 200, "country": "Canada",
              "address": { "street": "175 Commerce Drive", "city": "Thornhill", "products": ["sneakers", "shoes"] }
            }
        ]
    })"_json;

    auto add_op = coll1->add(doc.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    // search both simply nested and deeply nested array-of-objects
    auto results = coll1->search("One", {"locations.address"}, "", {}, sort_fields, {0}, 10, 1,
                                 token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                                 spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "locations.address").get();

    ASSERT_EQ(1, results["hits"].size());

    auto highlight_doc = R"({
      "locations":[
        {
          "address":{
            "street":"<mark>One</mark> Bowerman Drive"
          }
        },
        {
          "address":{
            "street":"175 Commerce Drive"
          }
        }
      ]
    })"_json;

    auto highlight_full_doc = R"({
        "locations":[
          {
            "address":{
              "city":"Beaverton",
              "products":[
                "shoes",
                "tshirts"
              ],
              "street":"<mark>One</mark> Bowerman Drive"
            }
          },
          {
            "address":{
              "city":"Thornhill",
              "products":[
                "sneakers",
                "shoes"
              ],
              "street":"175 Commerce Drive"
            }
          }
        ]
    })"_json;

    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());
    ASSERT_EQ(highlight_full_doc.dump(), results["hits"][0]["highlight"]["full"].dump());
    ASSERT_EQ(0, results["hits"][0]["highlights"].size());

    // repeating token

    results = coll1->search("drive", {"locations.address"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "locations.address").get();

    ASSERT_EQ(1, results["hits"].size());

    highlight_doc = R"({
      "locations":[
        {
          "address":{
            "street":"One Bowerman <mark>Drive</mark>"
          }
        },
        {
          "address":{
            "street":"175 Commerce <mark>Drive</mark>"
          }
        }
      ]
    })"_json;

    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());
    ASSERT_EQ(0, results["hits"][0]["highlights"].size());

    // nested array of array, highlighting parent of searched nested field
    results = coll1->search("shoes", {"locations.address.products"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "locations.address",
                            20, {}, {}, {}, 0, "<mark>", "</mark>", {}, 1000, true, false, true,
                            "locations.address").get();

    ASSERT_EQ(1, results["hits"].size());
    highlight_full_doc = R"({
      "locations":[
        {
          "address":{
            "city":"Beaverton",
            "products":[
              "<mark>shoes</mark>",
              "tshirts"
            ],
            "street":"One Bowerman Drive"
          }
        },
        {
          "address":{
            "city":"Thornhill",
            "products":[
              "sneakers",
              "<mark>shoes</mark>"
            ],
            "street":"175 Commerce Drive"
          }
        }
      ]
    })"_json;

    ASSERT_EQ(highlight_full_doc.dump(), results["hits"][0]["highlight"]["full"].dump());
    ASSERT_EQ(highlight_full_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());

    // full highlighting only one of the 3 highlight fields
    results = coll1->search("drive", {"company.names", "company_names", "locations.address"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "locations.address",
                            20, {}, {}, {}, 0, "<mark>", "</mark>", {}, 1000, true, false, true,
                            "company.names,company_names,locations.address").get();

    highlight_full_doc = R"({
        "locations":[
          {
            "address":{
              "city":"Beaverton",
              "products":[
                "shoes",
                "tshirts"
              ],
              "street":"One Bowerman <mark>Drive</mark>"
            }
          },
          {
            "address":{
              "city":"Thornhill",
              "products":[
                "sneakers",
                "shoes"
              ],
              "street":"175 Commerce <mark>Drive</mark>"
            }
          }
        ]
    })"_json;

    highlight_doc = R"({
        "company":{
          "names": ["Space Corp. LLC", "<mark>Drive</mark> One Inc."]
        },
        "company_names": ["Space Corp. LLC", "<mark>Drive</mark> One Inc."],
        "locations":[
          {
            "address":{
              "city":"Beaverton",
              "products":[
                "shoes",
                "tshirts"
              ],
              "street":"One Bowerman <mark>Drive</mark>"
            }
          },
          {
            "address":{
              "city":"Thornhill",
              "products":[
                "sneakers",
                "shoes"
              ],
              "street":"175 Commerce <mark>Drive</mark>"
            }
          }
        ]
    })"_json;

    ASSERT_EQ(highlight_full_doc.dump(), results["hits"][0]["highlight"]["full"].dump());
    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());

    // if highlight fields not provided, only matching sub-fields should appear in highlight

    results = coll1->search("space", {"company.names", "company_names", "locations.address"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    highlight_doc = R"({
        "company":{
          "names": ["<mark>Space</mark> Corp. LLC", "Drive One Inc."]
        },
        "company_names": ["<mark>Space</mark> Corp. LLC", "Drive One Inc."]
    })"_json;

    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());
    ASSERT_EQ(0, results["hits"][0]["highlight"]["full"].size());

    // only a single highlight full field provided

    results = coll1->search("space", {"company.names", "company_names", "locations.address"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "company.names").get();

    highlight_full_doc = R"({
      "company":{
        "names":[
          "<mark>Space</mark> Corp. LLC",
          "Drive One Inc."
        ]
      }
    })"_json;

    highlight_doc = R"({
      "company":{
        "names":[
          "<mark>Space</mark> Corp. LLC",
          "Drive One Inc."
        ]
      },
      "company_names":[
        "<mark>Space</mark> Corp. LLC",
        "Drive One Inc."
      ]
    })"_json;

    ASSERT_EQ(highlight_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());
    ASSERT_EQ(highlight_full_doc.dump(), results["hits"][0]["highlight"]["full"].dump());

    // try to highlight `id` field
    results = coll1->search("shoes", {"locations.address.products"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "id",
                            20, {}, {}, {}, 0, "<mark>", "</mark>", {}, 1000, true, false, true,
                            "id").get();

    ASSERT_TRUE(results["hits"][0]["highlight"]["snippet"].empty());
    ASSERT_TRUE(results["hits"][0]["highlight"]["full"].empty());
}

TEST_F(CollectionNestedFieldsTest, HighlightShouldHaveMeta) {
    std::vector<field> fields = {field(".*", field_types::AUTO, false, true)};

    auto op = collectionManager.create_collection("coll1", 1, fields, "", 0, field_types::AUTO, {}, {}, true);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc = R"({
        "company_names": ["Quick brown fox jumped.", "The red fox was not fast."],
        "details": {
            "description": "Quick set, go.",
            "names": ["Quick brown fox jumped.", "The red fox was not fast."]
        },
        "locations": [
            {
              "address": { "street": "Brown Shade Avenue" }
            },
            {
                "address": { "street": "Graywolf Lane" }
            }
        ]
    })"_json;

    auto add_op = coll1->add(doc.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    // search both simply nested and deeply nested array-of-objects
    auto results = coll1->search("brown fox", {"company_names", "details", "locations"},
                                 "", {}, sort_fields, {0}, 10, 1,
                                 token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                                 spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "locations.address").get();

    ASSERT_EQ(3, results["hits"][0]["highlight"]["meta"].size());
    ASSERT_EQ(1, results["hits"][0]["highlight"]["meta"]["company_names"].size());

    ASSERT_EQ(2, results["hits"][0]["highlight"]["meta"]["company_names"]["matched_tokens"].size());
    std::vector<std::string> matched_tokens = results["hits"][0]["highlight"]["meta"]["company_names"]["matched_tokens"].get<std::vector<std::string>>();
    std::sort(matched_tokens.begin(), matched_tokens.end());
    ASSERT_EQ("brown", matched_tokens[0]);
    ASSERT_EQ("fox", matched_tokens[1]);

    ASSERT_EQ(2, results["hits"][0]["highlight"]["meta"]["details.names"]["matched_tokens"].size());
    matched_tokens = results["hits"][0]["highlight"]["meta"]["details.names"]["matched_tokens"].get<std::vector<std::string>>();
    std::sort(matched_tokens.begin(), matched_tokens.end());
    ASSERT_EQ("brown", matched_tokens[0]);
    ASSERT_EQ("fox", matched_tokens[1]);

    ASSERT_EQ(1, results["hits"][0]["highlight"]["meta"]["locations.address.street"]["matched_tokens"].size());
    matched_tokens = results["hits"][0]["highlight"]["meta"]["locations.address.street"]["matched_tokens"].get<std::vector<std::string>>();
    std::sort(matched_tokens.begin(), matched_tokens.end());
    ASSERT_EQ("Brown", matched_tokens[0]);

    // when no highlighting is enabled by setting unknown field for highlighting
    results = coll1->search("brown fox", {"company_names", "details", "locations"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4, "x",
                            20, {}, {}, {}, 0, "<mark>", "</mark>", {}, 1000, true, false, true,
                            "x").get();

    ASSERT_EQ(2, results["hits"][0]["highlight"].size());
    ASSERT_EQ(0, results["hits"][0]["highlight"]["snippet"].size());
    ASSERT_EQ(0, results["hits"][0]["highlight"]["full"].size());
}

TEST_F(CollectionNestedFieldsTest, FieldsWithExplicitSchema) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": "details", "type": "object", "optional": false },
          {"name": "company.name", "type": "string", "optional": false },
          {"name": "locations", "type": "object[]", "optional": false }
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    nlohmann::json coll_summary = coll1->get_summary_json();
    ASSERT_EQ(1, coll_summary.count("enable_nested_fields"));

    auto doc = R"({
        "company_names": ["Quick brown fox jumped.", "The red fox was not fast."],
        "details": {
            "description": "Quick set, go.",
            "names": ["Quick brown fox jumped.", "The red fox was not fast."]
        },
        "company": {"name": "Quick and easy fix."},
        "locations": [
            {
                "address": { "street": "Brown Shade Avenue" }
            },
            {
                "address": { "street": "Graywolf Lane" }
            }
        ]
    })"_json;

    auto add_op = coll1->add(doc.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    // search both simply nested and deeply nested array-of-objects
    auto results = coll1->search("brown fox", {"details", "locations"},
                                 "", {}, sort_fields, {0}, 10, 1,
                                 token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                                 spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    auto snippet_doc = R"({
          "details":{
            "names":[
              "Quick <mark>brown</mark> <mark>fox</mark> jumped.",
              "The red <mark>fox</mark> was not fast."
            ]
          },
          "locations":[
            {
              "address":{
                "street":"<mark>Brown</mark> Shade Avenue"
              }
            },
            {
              "address":{
                "street":"Graywolf Lane"
              }
            }
          ]
    })"_json;

    ASSERT_EQ(1, results["hits"].size());
    ASSERT_EQ(snippet_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());

    results = coll1->search("fix", {"company.name"},
                            "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(1, results["hits"].size());

    // explicit nested array field (locations.address.street)
    schema = R"({
        "name": "coll2",
        "enable_nested_fields": true,
        "fields": [
          {"name": "details", "type": "object", "optional": false },
          {"name": "company.name", "type": "string", "optional": false },
          {"name": "locations.address.street", "type": "string[]", "optional": false }
        ]
    })"_json;

    op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll2 = op.get();

    add_op = coll2->add(doc.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    results = coll2->search("brown", {"locations.address.street"},
                            "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(1, results["hits"].size());

    snippet_doc = R"({
      "locations":[
        {
          "address":{
            "street":"<mark>Brown</mark> Shade Avenue"
          }
        },
        {
          "address":{
            "street":"Graywolf Lane"
          }
        }
      ]
    })"_json;

    ASSERT_EQ(snippet_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());

    // explicit partial array object field in the schema
    schema = R"({
        "name": "coll3",
        "enable_nested_fields": true,
        "fields": [
          {"name": "details", "type": "object", "optional": false },
          {"name": "company.name", "type": "string", "optional": false },
          {"name": "locations.address", "type": "object[]", "optional": false }
        ]
    })"_json;

    op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll3 = op.get();

    add_op = coll3->add(doc.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    results = coll3->search("brown", {"locations.address"},
                            "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(1, results["hits"].size());

    snippet_doc = R"({
      "locations":[
        {
          "address":{
            "street":"<mark>Brown</mark> Shade Avenue"
          }
        },
        {
          "address":{
            "street":"Graywolf Lane"
          }
        }
      ]
    })"_json;

    ASSERT_EQ(snippet_doc.dump(), results["hits"][0]["highlight"]["snippet"].dump());

    // non-optional object field validation (details)
    auto doc2 = R"({
        "company_names": ["Quick brown fox jumped.", "The red fox was not fast."],
        "company": {"name": "Quick and easy fix."},
        "locations": [
            {
                "address": { "street": "Foo bar street" }
            }
        ]
    })"_json;

    add_op = coll3->add(doc2.dump(), CREATE);
    ASSERT_FALSE(add_op.ok());
    ASSERT_EQ("Field `details` was not found or has an incorrect type.", add_op.error());

    // check fields and their properties
    auto coll_fields = coll1->get_fields();
    ASSERT_EQ(6, coll_fields.size());

    for(size_t i = 0; i < coll_fields.size(); i++) {
        auto& coll_field = coll_fields[i];
        if(i <= 2) {
            // original 3 explicit fields will be non-optional, but the sub-properties will be optional
            ASSERT_FALSE(coll_field.optional);
        } else {
            ASSERT_TRUE(coll_field.optional);
        }
    }
}

TEST_F(CollectionNestedFieldsTest, ExplicitSchemaOptionalFieldValidation) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": "details", "type": "object", "optional": true },
          {"name": "company.name", "type": "string", "optional": true },
          {"name": "locations", "type": "object[]", "optional": true }
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    // no optional field is present and that should be allowed
    auto doc1 = R"({
        "foo": "bar"
    })"_json;

    auto add_op = coll1->add(doc1.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    // some parts of an optional field is present in a subsequent doc indexed
    auto doc2 = R"({
        "details": {"name": "foo"}
    })"_json;
    add_op = coll1->add(doc2.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    auto doc3 = R"({
        "details": {"age": 30}
    })"_json;
    add_op = coll1->add(doc3.dump(), CREATE);
    ASSERT_TRUE(add_op.ok());

    // check fields and their properties
    auto coll_fields = coll1->get_fields();
    ASSERT_EQ(5, coll_fields.size());
    for(auto& coll_field : coll_fields) {
        ASSERT_TRUE(coll_field.optional);
    }
}

TEST_F(CollectionNestedFieldsTest, SortByNestedField) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": "details", "type": "object", "optional": false },
          {"name": "company.num_employees", "type": "int32", "optional": false }
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "details": {"count": 1000},
        "company": {"num_employees": 2000}
    })"_json;

    auto doc2 = R"({
        "details": {"count": 2000},
        "company": {"num_employees": 1000}
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());
    ASSERT_TRUE(coll1->add(doc2.dump(), CREATE).ok());

    std::vector<sort_by> sort_fields = { sort_by("details.count", "ASC") };

    auto results = coll1->search("*", {},
                                 "", {}, sort_fields, {0}, 10, 1,
                                 token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                                 spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(2, results["found"].get<size_t>());
    ASSERT_EQ(2, results["hits"].size());
    ASSERT_EQ("0", results["hits"][0]["document"]["id"].get<std::string>());
    ASSERT_EQ("1", results["hits"][1]["document"]["id"].get<std::string>());

    sort_fields = { sort_by("company.num_employees", "ASC") };
    results = coll1->search("*", {},
                            "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(2, results["found"].get<size_t>());
    ASSERT_EQ(2, results["hits"].size());
    ASSERT_EQ("1", results["hits"][0]["document"]["id"].get<std::string>());
    ASSERT_EQ("0", results["hits"][1]["document"]["id"].get<std::string>());

    // with auto schema
    schema = R"({
        "name": "coll2",
        "enable_nested_fields": true,
        "fields": [
          {"name": ".*", "type": "auto"}
        ]
    })"_json;

    op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll2 = op.get();

    ASSERT_TRUE(coll2->add(doc1.dump(), CREATE).ok());
    ASSERT_TRUE(coll2->add(doc2.dump(), CREATE).ok());

    sort_fields = { sort_by("details.count", "ASC") };

    results = coll2->search("*", {},
                             "", {}, sort_fields, {0}, 10, 1,
                             token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                             spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(2, results["found"].get<size_t>());
    ASSERT_EQ(2, results["hits"].size());
    ASSERT_EQ("0", results["hits"][0]["document"]["id"].get<std::string>());
    ASSERT_EQ("1", results["hits"][1]["document"]["id"].get<std::string>());

    sort_fields = { sort_by("company.num_employees", "ASC") };
    results = coll2->search("*", {},
                            "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}, 10, spp::sparse_hash_set<std::string>(),
                            spp::sparse_hash_set<std::string>(), 10, "", 30, 4).get();

    ASSERT_EQ(2, results["found"].get<size_t>());
    ASSERT_EQ(2, results["hits"].size());
    ASSERT_EQ("1", results["hits"][0]["document"]["id"].get<std::string>());
    ASSERT_EQ("0", results["hits"][1]["document"]["id"].get<std::string>());
}

TEST_F(CollectionNestedFieldsTest, OnlyExplcitSchemaFieldMustBeIndexedInADoc) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": "company.num_employees", "type": "int32", "optional": false },
          {"name": "company.founded", "type": "int32", "optional": false }
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "company": {"num_employees": 2000, "founded": 1976, "year": 2000}
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());
    auto fs = coll1->get_fields();
    ASSERT_EQ(2, coll1->get_fields().size());
}

TEST_F(CollectionNestedFieldsTest, VerifyDisableOfNestedFields) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "fields": [
          {"name": ".*", "type": "auto"}
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "company": {"num_employees": 2000, "founded": 1976, "year": 2000},
        "company_num_employees": 2000,
        "company_founded": 1976
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());
    auto fs = coll1->get_fields();
    ASSERT_EQ(3, coll1->get_fields().size());

    // explicit schema
    schema = R"({
        "name": "coll2",
        "fields": [
          {"name": "company_num_employees", "type": "int32"},
          {"name": "company_founded", "type": "int32"}
        ]
    })"_json;

    op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll2 = op.get();

    ASSERT_TRUE(coll2->add(doc1.dump(), CREATE).ok());
    fs = coll2->get_fields();
    ASSERT_EQ(2, coll2->get_fields().size());
}

TEST_F(CollectionNestedFieldsTest, ExplicitDotSeparatedFieldsShouldHavePrecendence) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": ".*", "type": "auto"}
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "company": {"num_employees": 1000, "ids": [1,2]},
        "details": [{"name": "bar"}],
        "company.num_employees": 2000,
        "company.ids": [10],
        "details.name": "foo"
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());
    auto fs = coll1->get_fields();
    ASSERT_EQ(4, coll1->get_fields().size());

    // simple nested object
    auto results = coll1->search("*", {}, "company.num_employees: 2000", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("*", {}, "company.num_employees: 1000", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    // nested array object
    results = coll1->search("foo", {"details.name"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("bar", {"details.name"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    // nested simple array
    results = coll1->search("*", {}, "company.ids: 10", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("*", {}, "company.ids: 1", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    // WITH EXPLICIT SCHEMA

    schema = R"({
        "name": "coll2",
        "enable_nested_fields": true,
        "fields": [
          {"name": "company.num_employees", "type": "int32"},
          {"name": "company.ids", "type": "int32[]"},
          {"name": "details.name", "type": "string[]"}
        ]
    })"_json;

    op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll2 = op.get();

    auto doc2 = R"({
        "company": {"num_employees": 1000, "ids": [1,2]},
        "details": [{"name": "bar"}],
        "company.num_employees": 2000,
        "company.ids": [10],
        "details.name": ["foo"]
    })"_json;

    ASSERT_TRUE(coll2->add(doc2.dump(), CREATE).ok());

    // simple nested object
    results = coll2->search("*", {}, "company.num_employees: 2000", {}, sort_fields, {0}, 10, 1,
                                 token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll2->search("*", {}, "company.num_employees: 1000", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    // nested array object
    results = coll2->search("foo", {"details.name"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll2->search("bar", {"details.name"}, "", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    // nested simple array
    results = coll2->search("*", {}, "company.ids: 10", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll2->search("*", {}, "company.ids: 1", {}, sort_fields, {0}, 10, 1,
                            token_ordering::FREQUENCY, {true}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

}

TEST_F(CollectionNestedFieldsTest, GroupByOnNestedFieldsWithWildcardSchema) {
    std::vector<field> fields = {field(".*", field_types::AUTO, false, true),
                                 field("education.name", field_types::STRING_ARRAY, true, true),
                                 field("employee.num", field_types::INT32, true, true)};

    auto op = collectionManager.create_collection("coll1", 1, fields, "", 0, field_types::AUTO, {}, {},
                                                  true);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "employee": {"num": 5000},
        "education": [
            {"name": "X High School", "type": "school"},
            {"name": "Y University", "type": "undergraduate"}
        ]
    })"_json;

    auto doc2 = R"({
        "employee": {"num": 1000},
        "education": [
            {"name": "X High School", "type": "school"},
            {"name": "Z University", "type": "undergraduate"}
        ]
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());
    ASSERT_TRUE(coll1->add(doc2.dump(), CREATE).ok());

    // group on a field inside array of objects
    auto results = coll1->search("school", {"education"}, "", {}, {}, {0}, 10, 1, FREQUENCY, {false}, 10,
                                 spp::sparse_hash_set<std::string>(), spp::sparse_hash_set<std::string>(), 10, "", 30,
                                 5, "", 10, {}, {}, {"education.name"}, 2).get();

    ASSERT_EQ(2, results["found"].get<size_t>());
    ASSERT_EQ(2, results["grouped_hits"].size());

    ASSERT_EQ(1, results["grouped_hits"][0]["group_key"].size());
    ASSERT_EQ(2, results["grouped_hits"][0]["group_key"][0].size());
    ASSERT_EQ("X High School", results["grouped_hits"][0]["group_key"][0][0].get<std::string>());
    ASSERT_EQ("Z University", results["grouped_hits"][0]["group_key"][0][1].get<std::string>());
    ASSERT_EQ(1, results["grouped_hits"][0]["hits"].size());
    ASSERT_EQ("1", results["grouped_hits"][0]["hits"][0]["document"]["id"].get<std::string>());

    ASSERT_EQ(1, results["grouped_hits"][1]["group_key"].size());
    ASSERT_EQ(2, results["grouped_hits"][1]["group_key"][0].size());
    ASSERT_EQ("X High School", results["grouped_hits"][1]["group_key"][0][0].get<std::string>());
    ASSERT_EQ("Y University", results["grouped_hits"][1]["group_key"][0][1].get<std::string>());
    ASSERT_EQ(1, results["grouped_hits"][1]["hits"].size());
    ASSERT_EQ("0", results["grouped_hits"][1]["hits"][0]["document"]["id"].get<std::string>());

    // group on plain nested field
    results = coll1->search("school", {"education"}, "", {}, {}, {0}, 10, 1, FREQUENCY, {false}, 10,
                            spp::sparse_hash_set<std::string>(), spp::sparse_hash_set<std::string>(), 10, "", 30,
                            5, "", 10, {}, {}, {"employee.num"}, 2).get();

    ASSERT_EQ(2, results["found"].get<size_t>());
    ASSERT_EQ(2, results["grouped_hits"].size());

    ASSERT_EQ(1, results["grouped_hits"][0]["group_key"].size());
    ASSERT_EQ(1, results["grouped_hits"][0]["group_key"][0].size());
    ASSERT_EQ(1000, results["grouped_hits"][0]["group_key"][0].get<size_t>());
    ASSERT_EQ(1, results["grouped_hits"][0]["hits"].size());
    ASSERT_EQ("1", results["grouped_hits"][0]["hits"][0]["document"]["id"].get<std::string>());

    ASSERT_EQ(1, results["grouped_hits"][1]["group_key"].size());
    ASSERT_EQ(1, results["grouped_hits"][1]["group_key"][0].size());
    ASSERT_EQ(5000, results["grouped_hits"][1]["group_key"][0].get<size_t>());
    ASSERT_EQ(1, results["grouped_hits"][1]["hits"].size());
    ASSERT_EQ("0", results["grouped_hits"][1]["hits"][0]["document"]["id"].get<std::string>());
}

TEST_F(CollectionNestedFieldsTest, WildcardWithExplicitSchema) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": ".*", "type": "auto"},
          {"name": "company.id", "type": "int32"},
          {"name": "studies.year", "type": "int32[]"}
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "id": "0",
        "company": {"id": 1000, "name": "Foo"},
        "studies": [{"name": "College 1", "year": 1997}]
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());

    auto results = coll1->search("*", {}, "company.id: 1000", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("*", {}, "studies.year: 1997", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());
}

TEST_F(CollectionNestedFieldsTest, UpdateOfNestFields) {
    nlohmann::json schema = R"({
        "name": "coll1",
        "enable_nested_fields": true,
        "fields": [
          {"name": ".*", "type": "auto"}
        ]
    })"_json;

    auto op = collectionManager.create_collection(schema);
    ASSERT_TRUE(op.ok());
    Collection* coll1 = op.get();

    auto doc1 = R"({
        "id": "0",
        "company": {"num_employees": 2000, "founded": 1976},
        "studies": [{"name": "College 1"}]
    })"_json;

    ASSERT_TRUE(coll1->add(doc1.dump(), CREATE).ok());

    auto doc_update = R"({
        "id": "0",
        "company": {"num_employees": 2000, "founded": 1976, "year": 2000},
        "studies": [{"name": "College Alpha", "year": 1967},{"name": "College Beta", "year": 1978}]
    })"_json;
    ASSERT_TRUE(coll1->add(doc_update.dump(), UPDATE).ok());

    auto results = coll1->search("*", {}, "company.year: 2000", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("*", {}, "studies.year: 1967", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("*", {}, "studies.year: 1978", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("alpha", {"studies.name"}, "", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    results = coll1->search("beta", {"studies.name"}, "", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    // try removing fields via upsert
    doc_update = R"({
        "id": "0",
        "company": {"num_employees": 2000, "founded": 1976},
        "studies": [{"name": "College Alpha"}]
    })"_json;
    ASSERT_TRUE(coll1->add(doc_update.dump(), UPSERT).ok());

    results = coll1->search("*", {}, "company.year: 2000", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    results = coll1->search("*", {}, "studies.year: 1967", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    results = coll1->search("*", {}, "studies.year: 1978", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(0, results["found"].get<size_t>());

    results = coll1->search("*", {}, "", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());
    ASSERT_EQ(3, results["hits"][0]["document"].size());
    ASSERT_EQ(2, results["hits"][0]["document"]["company"].size());
    ASSERT_EQ(2000, results["hits"][0]["document"]["company"]["num_employees"].get<size_t>());
    ASSERT_EQ(1976, results["hits"][0]["document"]["company"]["founded"].get<size_t>());
    ASSERT_EQ(1, results["hits"][0]["document"]["studies"].size());
    ASSERT_EQ(1, results["hits"][0]["document"]["studies"][0].size());
    ASSERT_EQ("College Alpha", results["hits"][0]["document"]["studies"][0]["name"].get<std::string>());

    // via update (should not remove, since document can be partial)
    doc_update = R"({
        "id": "0",
        "company": {"num_employees": 2000},
        "studies": [{"name": "College Alpha"}]
    })"_json;
    ASSERT_TRUE(coll1->add(doc_update.dump(), UPDATE).ok());

    results = coll1->search("*", {}, "company.founded: 1976", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());

    // via emplace (should not remove, since document can be partial)
    doc_update = R"({
        "id": "0",
        "company": {},
        "studies": [{"name": "College Alpha", "year": 1977}]
    })"_json;
    ASSERT_TRUE(coll1->add(doc_update.dump(), EMPLACE).ok());

    results = coll1->search("*", {}, "company.num_employees: 2000", {}, {}, {0}, 10, 1, FREQUENCY, {false}).get();
    ASSERT_EQ(1, results["found"].get<size_t>());
}