//
// Created by fss on 22-11-13.
//
#include "parser/ir.hpp"
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <stack>

#include <glog/logging.h>

namespace kuiper_infer {

static size_t TypeToElementSize(int type) {
  if (type == 1) return 4;
  if (type == 2) return 8;
  if (type == 3) return 2;
  if (type == 4) return 4;
  if (type == 5) return 8;
  if (type == 6) return 2;
  if (type == 7) return 1;
  if (type == 8) return 1;
  if (type == 9) return 1;
  if (type == 10) return 8;
  if (type == 11) return 16;
  if (type == 12) return 4;
  return 0; // null
}

static int StringToType(const char *s) {
  if (strcmp(s, "f32") == 0) return 1;
  if (strcmp(s, "f64") == 0) return 2;
  if (strcmp(s, "f16") == 0) return 3;
  if (strcmp(s, "i32") == 0) return 4;
  if (strcmp(s, "i64") == 0) return 5;
  if (strcmp(s, "i16") == 0) return 6;
  if (strcmp(s, "i8") == 0) return 7;
  if (strcmp(s, "u8") == 0) return 8;
  if (strcmp(s, "bool") == 0) return 9;
  if (strcmp(s, "cp64") == 0) return 10;
  if (strcmp(s, "cp128") == 0) return 11;
  if (strcmp(s, "cp32") == 0) return 12;
  return 0; // null
}

bool operator==(const Parameter &p1, const Parameter &p2) {
  if (p1.type != p2.type)
    return false;

  if (p1.type == 0)
    return true;

  if (p1.type == 1 && p1.b == p2.b)
    return true;

  if (p1.type == 2 && p1.i == p2.i)
    return true;

  if (p1.type == 3 && p1.f == p2.f)
    return true;

  if (p1.type == 4 && p1.str == p2.str)
    return true;

  if (p1.type == 5 && p1.int_array == p2.int_array)
    return true;

  if (p1.type == 6 && p1.float_array == p2.float_array)
    return true;

  if (p1.type == 7 && p1.str_array == p2.str_array)
    return true;

  return false;
}

Attribute::Attribute(const std::initializer_list<int> &shape_list, const std::vector<float> &raw_data) {
  type = 1;
  shape = shape_list;

  if (!shape.empty()) {
    int size = shape[0];
    for (size_t i = 1; i < shape.size(); i++) {
      size *= shape[i];
    }

    data.resize(size * TypeToElementSize(type));
    memcpy((void *) data.data(), (const void *) raw_data.data(), data.size());
  }
}

bool operator==(const Attribute &attr1, const Attribute &attr2) {
  if (attr1.type != attr2.type)
    return false;

  if (attr1.type == 0)
    return true;

  if (attr1.shape != attr2.shape)
    return false;

  if (attr1.data != attr2.data)
    return false;

  return true;
}

Attribute operator+(const Attribute &attr1, const Attribute &attr2) {
  Attribute c;

  if (attr1.type != attr2.type) {
    LOG(ERROR) << "Concat attribute type mismatch";
    return c;
  }

  if (attr1.shape.size() != attr2.shape.size()) {
    LOG(ERROR) << "Concat attribute shape rank mismatch";
    return c;
  }

  for (int i = 1; i < (int) attr1.shape.size(); i++) {
    if (attr1.shape[i] != attr2.shape[i]) {
      LOG(ERROR) << "Concat attribute shape mismatch";
      return c;
    }
  }

  c.type = attr1.type;
  c.shape = attr1.shape;
  c.shape[0] += attr2.shape[0]; // concat the first dim

  c.data.resize(attr1.data.size() + attr2.data.size());
  memcpy(c.data.data(), attr1.data.data(), attr1.data.size());
  memcpy(c.data.data() + attr1.data.size(), attr2.data.data(), attr2.data.size());
  return c;
}

Parameter Parameter::parse_from_string(const std::string &value) {
  Parameter p;
  p.type = 0;

  if (value == "None" || value == "()" || value == "[]") {
    return p;
  }

  if (value == "True" || value == "False") {
    // bool
    p.type = 1;
    p.b = value == "True";
    return p;
  }

  if (value[0] == '(' || value[0] == '[') {
    // list
    std::string lc = value.substr(1, value.size() - 2);
    std::istringstream lcss(lc);

    while (!lcss.eof()) {
      std::string elem;
      std::getline(lcss, elem, ',');

      if ((elem[0] != '-' && (elem[0] < '0' || elem[0] > '9'))
          || (elem[0] == '-' && (elem[1] < '0' || elem[1] > '9'))) {
        // string
        p.type = 7;
        p.str_array.push_back(elem);
      } else if (elem.find('.') != std::string::npos || elem.find('e') != std::string::npos) {
        // float
        p.type = 6;
        p.float_array.push_back(std::stof(elem));
      } else {
        // integer
        p.type = 5;
        p.int_array.push_back(std::stoi(elem));
      }
    }
    return p;
  }

  if ((value[0] != '-' && (value[0] < '0' || value[0] > '9'))
      || (value[0] == '-' && (value[1] < '0' || value[1] > '9'))) {
    // string
    p.type = 4;
    p.str = value;
    return p;
  }

  if (value.find('.') != std::string::npos || value.find('e') != std::string::npos) {
    // float
    p.type = 3;
    p.f = std::stof(value);
    return p;
  }

  // integer
  p.type = 2;
  p.i = std::stoi(value);
  return p;
}

Graph::~Graph() {
  for (auto x : ops) {
    if (x != nullptr)
      delete x;
  }

  for (auto x : operands) {
    if (x != nullptr)
      delete x;
  }

  ops.clear();
  operands.clear();
}

static void LoadParameter(Operator *op, const std::string &key, const std::string &value) {
  op->params[key] = Parameter::parse_from_string(value);
}

static void LoadInputKey(Operator *op, const std::string &key, const std::string &value) {
  op->input_names.resize(op->inputs.size());

  for (size_t i = 0; i < op->inputs.size(); i++) {
    const Operand *oprand = op->inputs[i];
    if (oprand->name == value) {
      op->input_names[i] = key;
      break;
    }
  }
}

static void LoadShape(Operator *op, const std::string &key, const std::string &value) {
  Operand *operand = nullptr;
  for (auto r : op->inputs) {
    if (r->name == key) {
      operand = r;
      break;
    }
  }

  if (!operand) {
    for (auto r : op->outputs) {
      if (r->name == key) {
        operand = r;
        break;
      }
    }
  }

  if (!operand) {
    const uint32_t error_size = 64;
    char error_buf[error_size];
    snprintf(error_buf, error_size - 1, "no such operand %str for operator %str\n", key.c_str(), op->name.c_str());
    LOG(ERROR) << error_buf;
    return;
  }

  // type
  std::string type_str = value.substr(value.find_last_of(')') + 1);
  operand->type = StringToType(type_str.c_str());

  // shape
  std::string lc = value.substr(1, value.find_last_of(')') - 1);
  std::istringstream lcss(lc);

  operand->shape.clear();
  while (!lcss.eof()) {
    std::string elem;
    std::getline(lcss, elem, ',');

    if (elem == "?") {
      operand->shape.push_back(-1);
    } else {
      int i = std::stoi(elem);
      operand->shape.push_back(i);
    }
  }
}

static void LoadAttribute(Operator *op, const std::string &key, const std::string &value, StoreZipReader &szr) {
  Attribute &attribute = op->attrs[key];

  // type
  std::string type_str = value.substr(value.find_last_of(')') + 1);
  attribute.type = StringToType(type_str.c_str());

  if (attribute.type == 0)
    return;

  // shape
  std::string lc = value.substr(1, value.find_last_of(')') - 1);
  std::istringstream lcss(lc);

  attribute.shape.clear();
  while (!lcss.eof()) {
    std::string elem;
    std::getline(lcss, elem, ',');

    int i = std::stoi(elem);
    attribute.shape.push_back(i);
  }

  if (attribute.shape.empty())
    return;

  // data
  size_t size = 1;
  for (int i : attribute.shape) {
    size *= i;
  }

  size_t byte_size = size * TypeToElementSize(attribute.type);
  std::string file_name = op->name + "." + key;
  size_t file_size = szr.get_file_size(file_name);

  if (file_size == 0) {
    return;
  }

  if (file_size != byte_size) {
    const uint32_t error_size = 64;
    char error_buf[error_size];
    snprintf(error_buf, error_size - 1, "file size not match expect %lu but got %lu\n", byte_size, file_size);
    LOG(ERROR) << error_buf;
  }

  attribute.data.resize(byte_size);
  szr.read_file(file_name, (char *) attribute.data.data());
}

int Graph::load(const std::string &param_path, const std::string &bin_path) {
  std::ifstream is(param_path, std::ios::in | std::ios::binary);
  if (!is.good()) {
    LOG(ERROR) << "Open failed!";
    return -1;
  }

  StoreZipReader szr;
  if (szr.open(bin_path) != 0) {
    LOG(ERROR) << "Open failed!";
    return -1;
  }

  int magic = 0;
  {
    std::string line;
    std::getline(is, line);
    std::istringstream iss(line);

    iss >> magic;
  }

  int operator_count = 0;
  int operand_count = 0;
  {
    std::string line;
    std::getline(is, line);
    std::istringstream iss(line);

    iss >> operator_count >> operand_count;
  }

  for (int i = 0; i < operator_count; i++) {
    std::string line;
    std::getline(is, line);
    std::istringstream iss(line);

    std::string type;
    std::string name;
    int input_count = 0;
    int output_count = 0;

    iss >> type >> name >> input_count >> output_count;

    Operator *op = NewOperator(type, name);

    for (int j = 0; j < input_count; j++) {
      std::string operand_name;
      iss >> operand_name;

      Operand *r = GetOperand(operand_name);
      if (r != nullptr) {
        r->consumers.push_back(op);
        op->inputs.push_back(r);
      }
    }

    for (int j = 0; j < output_count; j++) {
      std::string operand_name;
      iss >> operand_name;

      Operand *r = NewOperand(operand_name);
      r->producer = op;
      op->outputs.push_back(r);
    }

    // key=value
    while (!iss.eof()) {
      std::string param;
      iss >> param;

      std::string key;
      std::string value;
      std::istringstream pss(param);
      std::getline(pss, key, '=');
      std::getline(pss, value);

      if (key[0] == '@') {
        // attribute
        LoadAttribute(op, key.substr(1), value, szr);
      } else if (key[0] == '$') {
        // operand input key
        LoadInputKey(op, key.substr(1), value);
      } else if (key[0] == '#') {
        // operand shape
        LoadShape(op, key.substr(1), value);
      } else {
        // parameter
        LoadParameter(op, key, value);
      }
    }
  }

  return 0;
}

Operator *Graph::NewOperator(const std::string &type, const std::string &name) {
  Operator *op = new Operator;
  op->type = type;
  op->name = name;
  ops.push_back(op);
  return op;
}

Operand *Graph::NewOperand(const std::string &name) {
  Operand *r = new Operand;
  r->name = name;
  operands.push_back(r);
  return r;
}

Operand *Graph::GetOperand(const std::string &name) {
  for (Operand *r : operands) {
    if (r->name == name)
      return r;
  }
  return nullptr;
}

const Operand *Graph::GetOperand(const std::string &name) const {
  for (const Operand *r : operands) {
    if (r->name == name)
      return r;
  }
  return nullptr;
}
}