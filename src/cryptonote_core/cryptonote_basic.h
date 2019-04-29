// Copyright (c) 2012-2013 The Cryptonote developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <boost/variant.hpp>
#include <boost/functional/hash/hash.hpp>
#include <iostream>
#include <vector>
#include <cstring>  // memcmp
#include <sstream>
#include "serialization/serialization.h"
#include "serialization/variant.h"
#include "serialization/vector.h"
#include "serialization/binary_archive.h"
#include "serialization/json_archive.h"
#include "serialization/debug_archive.h"
#include "serialization/crypto.h"
#include "serialization/keyvalue_serialization.h" // eepe named serialization
#include "string_tools.h"
#include "cryptonote_config.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "misc_language.h"
#include "tx_extra.h"
#include "ringct/rctTypes.h"
#include "cryptonote_protocol/blobdatatype.h"


namespace cryptonote
{
  struct block;
  class transaction;
  class transaction_prefix;
  struct tx_extra_merge_mining_tag;

  // Implemented in cryptonote_format_utils.cpp
  bool get_transaction_hash(const transaction& t, crypto::hash& res);
  void get_transaction_prefix_hash(const transaction_prefix& tx, crypto::hash& h);
  void get_blob_hash(const blobdata& blob, crypto::hash& res);
  bool get_mm_tag_from_extra(const std::vector<uint8_t>& tx, tx_extra_merge_mining_tag& mm_tag);

  const static crypto::hash null_hash = AUTO_VAL_INIT(null_hash);
  const static crypto::public_key null_pkey = AUTO_VAL_INIT(null_pkey);

  typedef std::vector<crypto::signature> ring_signature;


  /* outputs */

  struct txout_to_script
  {
    std::vector<crypto::public_key> keys;
    std::vector<uint8_t> script;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(keys)
      FIELD(script)
    END_SERIALIZE()
  };

  struct txout_to_scripthash
  {
    crypto::hash hash;
  };

  struct txout_to_key
  {
    txout_to_key() { }
    txout_to_key(const crypto::public_key &_key) : key(_key) { }
    crypto::public_key key;
  };

  /* inputs */

  struct txin_gen
  {
    size_t height;

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD(height)
    END_SERIALIZE()
  };

  struct txin_to_script
  {
    crypto::hash prev;
    size_t prevout;
    std::vector<uint8_t> sigset;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(prev)
      VARINT_FIELD(prevout)
      FIELD(sigset)
    END_SERIALIZE()
  };

  struct txin_to_scripthash
  {
    crypto::hash prev;
    size_t prevout;
    txout_to_script script;
    std::vector<uint8_t> sigset;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(prev)
      VARINT_FIELD(prevout)
      FIELD(script)
      FIELD(sigset)
    END_SERIALIZE()
  };

  struct txin_to_key
  {
    uint64_t amount;
    std::vector<uint64_t> key_offsets;
    crypto::key_image k_image;      // double spending protection

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD(amount)
      FIELD(key_offsets)
      FIELD(k_image)
    END_SERIALIZE()
  };


  typedef boost::variant<txin_gen, txin_to_script, txin_to_scripthash, txin_to_key> txin_v;

  typedef boost::variant<txout_to_script, txout_to_scripthash, txout_to_key> txout_target_v;

  //typedef std::pair<uint64_t, txout> out_t;
  struct tx_out
  {
    uint64_t amount;
    txout_target_v target;

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD(amount)
      FIELD(target)
    END_SERIALIZE()
  };

  enum loki_version
  {
    loki_version_0 = 0,
    loki_version_1,
    loki_version_2,
    loki_version_3_per_output_unlock_times,
    loki_version_4_tx_types,
  };

  class transaction_prefix
  {

  public:
    enum BLOB_TYPE blob_type;
    // tx information
    size_t   version;
    uint64_t unlock_time;  //number of block (or time), used as a limitation like: spend this tx not early then block/time

    std::vector<txin_v> vin;
    std::vector<tx_out> vout;
    //extra
    std::vector<uint8_t> extra;

    //
    // NOTE: Loki specific
    //
    std::vector<uint64_t> output_unlock_times;
    enum loki_type_t
    {
      loki_type_standard,
      loki_type_deregister,
      loki_type_key_image_unlock,
      loki_type_count,
    };

    union
    {
      bool is_deregister;
      uint16_t type;
    };

    BEGIN_SERIALIZE()
      VARINT_FIELD(version)
      if (version > loki_version_2 && blob_type == BLOB_TYPE_CRYPTONOTE_LOKI)
      {
        FIELD(output_unlock_times)
        if (version == loki_version_3_per_output_unlock_times)
          FIELD(is_deregister)
      }
      VARINT_FIELD(unlock_time)
      FIELD(vin)
      FIELD(vout)
      if (blob_type == BLOB_TYPE_CRYPTONOTE_LOKI)
      {
        if (version >= loki_version_3_per_output_unlock_times && vout.size() != output_unlock_times.size()) return false;
      }
      FIELD(extra)
      if (blob_type == BLOB_TYPE_CRYPTONOTE_LOKI && version >= loki_version_4_tx_types)
      {
        VARINT_FIELD(type)
        if (static_cast<uint16_t>(type) >= loki_type_count) return false;
      }
    END_SERIALIZE()


  protected:
    transaction_prefix() : blob_type(BLOB_TYPE_CRYPTONOTE) {}
  };

  class transaction: public transaction_prefix
  {
  public:
    std::vector<std::vector<crypto::signature> > signatures; //count signatures  always the same as inputs count
    rct::rctSig rct_signatures;

    transaction();
    virtual ~transaction();
    void set_null();

    BEGIN_SERIALIZE_OBJECT()
      FIELDS(*static_cast<transaction_prefix *>(this))

      if (version == 1 && blob_type != BLOB_TYPE_CRYPTONOTE2)
      {
        ar.tag("signatures");
        ar.begin_array();
        PREPARE_CUSTOM_VECTOR_SERIALIZATION(vin.size(), signatures);
        bool signatures_not_expected = signatures.empty();
        if (!signatures_not_expected && vin.size() != signatures.size())
          return false;

        for (size_t i = 0; i < vin.size(); ++i)
        {
          size_t signature_size = get_signature_size(vin[i]);
          if (signatures_not_expected)
          {
            if (0 == signature_size)
              continue;
            else
              return false;
          }

          PREPARE_CUSTOM_VECTOR_SERIALIZATION(signature_size, signatures[i]);
          if (signature_size != signatures[i].size())
            return false;

          FIELDS(signatures[i]);

          if (vin.size() - i > 1)
            ar.delimit_array();
        }
        ar.end_array();
      }
      else
      {
        ar.tag("rct_signatures");
        if (!vin.empty())
        {
          ar.begin_object();
          bool r = rct_signatures.serialize_rctsig_base(ar, vin.size(), vout.size());
          if (!r || !ar.stream().good()) return false;
          ar.end_object();
          if (rct_signatures.type != rct::RCTTypeNull)
          {
            ar.tag("rctsig_prunable");
            ar.begin_object();
            r = rct_signatures.p.serialize_rctsig_prunable(ar, rct_signatures.type, vin.size(), vout.size(),
                vin[0].type() == typeid(txin_to_key) ? boost::get<txin_to_key>(vin[0]).key_offsets.size() - 1 : 0);
            if (!r || !ar.stream().good()) return false;
            ar.end_object();
          }
        }
      }
    END_SERIALIZE()

  private:
    static size_t get_signature_size(const txin_v& tx_in);
  };

  inline
  transaction::transaction()
  {
    set_null();
  }

  inline
  transaction::~transaction()
  {
    //set_null();
  }

  inline
  void transaction::set_null()
  {
    version = 0;
    unlock_time = 0;
    vin.clear();
    vout.clear();
    extra.clear();
    signatures.clear();
  }

  inline
  size_t transaction::get_signature_size(const txin_v& tx_in)
  {
    struct txin_signature_size_visitor : public boost::static_visitor<size_t>
    {
      size_t operator()(const txin_gen& txin) const{return 0;}
      size_t operator()(const txin_to_script& txin) const{return 0;}
      size_t operator()(const txin_to_scripthash& txin) const{return 0;}
      size_t operator()(const txin_to_key& txin) const {return txin.key_offsets.size();}
    };

    return boost::apply_visitor(txin_signature_size_visitor(), tx_in);
  }

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/

  const uint8_t CURRENT_BYTECOIN_BLOCK_MAJOR_VERSION = 1;

  struct bytecoin_block
  {
    uint8_t major_version;
    uint8_t minor_version;
    crypto::hash prev_id;
    uint32_t nonce;
    size_t number_of_transactions;
    std::vector<crypto::hash> miner_tx_branch;
    transaction miner_tx;
    std::vector<crypto::hash> blockchain_branch;
  };

  struct serializable_bytecoin_block
  {
    bytecoin_block& b;
    uint64_t& timestamp;
    bool hashing_serialization;
    bool header_only;

    serializable_bytecoin_block(bytecoin_block& b_, uint64_t& timestamp_, bool hashing_serialization_, bool header_only_) :
      b(b_), timestamp(timestamp_), hashing_serialization(hashing_serialization_), header_only(header_only_)
    {
    }

    BEGIN_SERIALIZE_OBJECT()
      VARINT_FIELD_N("major_version", b.major_version);
      VARINT_FIELD_N("minor_version", b.minor_version);
      VARINT_FIELD(timestamp);
      FIELD_N("prev_id", b.prev_id);
      FIELD_N("nonce", b.nonce);

      if (hashing_serialization)
      {
        crypto::hash miner_tx_hash;

        if (b.miner_tx.version < 2) {
          if (!get_transaction_hash(b.miner_tx, miner_tx_hash))
            return false;
        } else {
          get_transaction_prefix_hash(static_cast<const transaction_prefix&>(b.miner_tx), miner_tx_hash);
          const uint8_t data[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbc, 0x36, 0x78, 0x9e, 0x7a, 0x1e, 0x28, 0x14, 0x36, 0x46, 0x42, 0x29, 0x82, 0x8f, 0x81, 0x7d, 0x66, 0x12, 0xf7, 0xb4, 0x77, 0xd6, 0x65, 0x91, 0xff, 0x96, 0xa9, 0xe0, 0x64, 0xbc, 0xc9, 0x8a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
          blobdata blobdata((char*)data, sizeof(data));
          const unsigned char* p = (unsigned char*)&miner_tx_hash;
          for (int i = 0; i != HASH_SIZE; ++ i, ++ p) blobdata[i] = *p;
          get_blob_hash(blobdata, miner_tx_hash);
        }

        crypto::hash merkle_root;
        crypto::tree_hash_from_branch(b.miner_tx_branch.data(), b.miner_tx_branch.size(), miner_tx_hash, 0, merkle_root);

        FIELD(merkle_root);
      }

      VARINT_FIELD_N("number_of_transactions", b.number_of_transactions);
      if (b.number_of_transactions < 1)
        return false;

      if (!header_only)
      {
        ar.tag("miner_tx_branch");
        ar.begin_array();
        size_t branch_size = crypto::tree_depth(b.number_of_transactions);
        PREPARE_CUSTOM_VECTOR_SERIALIZATION(branch_size, const_cast<bytecoin_block&>(b).miner_tx_branch);
        if (b.miner_tx_branch.size() != branch_size)
          return false;
        for (size_t i = 0; i < branch_size; ++i)
        {
          FIELDS(b.miner_tx_branch[i]);
          if (i + 1 < branch_size)
            ar.delimit_array();
        }
        ar.end_array();

        FIELD(b.miner_tx);

        tx_extra_merge_mining_tag mm_tag;
        if (!get_mm_tag_from_extra(b.miner_tx.extra, mm_tag))
          return false;

        ar.tag("blockchain_branch");
        ar.begin_array();
        PREPARE_CUSTOM_VECTOR_SERIALIZATION(mm_tag.depth, const_cast<bytecoin_block&>(b).blockchain_branch);
        if (mm_tag.depth != b.blockchain_branch.size())
          return false;
        for (size_t i = 0; i < mm_tag.depth; ++i)
        {
          FIELDS(b.blockchain_branch[i]);
          if (i + 1 < mm_tag.depth)
            ar.delimit_array();
        }
        ar.end_array();
      }
    END_SERIALIZE()
  };

  // Implemented below
  inline serializable_bytecoin_block make_serializable_bytecoin_block(const block& b, bool hashing_serialization, bool header_only);

  struct block_header
  {
    enum BLOB_TYPE blob_type;

    uint8_t major_version;
    uint8_t minor_version;
    uint64_t timestamp;
    crypto::hash prev_id;
    uint32_t nonce;

    BEGIN_SERIALIZE()
      VARINT_FIELD(major_version)
      VARINT_FIELD(minor_version)
      if (blob_type != BLOB_TYPE_FORKNOTE2) VARINT_FIELD(timestamp)
      FIELD(prev_id)
      if (blob_type != BLOB_TYPE_FORKNOTE2) FIELD(nonce)
    END_SERIALIZE()
  };

  struct block: public block_header
  {
    bytecoin_block parent_block;

    transaction miner_tx;
    std::vector<crypto::hash> tx_hashes;

    void set_blob_type(enum BLOB_TYPE bt) { miner_tx.blob_type = blob_type = bt; }

    BEGIN_SERIALIZE_OBJECT()
      FIELDS(*static_cast<block_header *>(this))
      if (blob_type == BLOB_TYPE_FORKNOTE2)
      {
        auto sbb = make_serializable_bytecoin_block(*this, false, false);
        FIELD_N("parent_block", sbb);
      }
      FIELD(miner_tx)
      FIELD(tx_hashes)
    END_SERIALIZE()
  };

  inline serializable_bytecoin_block make_serializable_bytecoin_block(const block& b, bool hashing_serialization, bool header_only)
  {
    block& block_ref = const_cast<block&>(b);
    return serializable_bytecoin_block(block_ref.parent_block, block_ref.timestamp, hashing_serialization, header_only);
  }

  /************************************************************************/
  /*                                                                      */
  /************************************************************************/
  struct account_public_address
  {
    crypto::public_key m_spend_public_key;
    crypto::public_key m_view_public_key;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(m_spend_public_key)
      FIELD(m_view_public_key)
    END_SERIALIZE()

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(m_spend_public_key)
      KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(m_view_public_key)
    END_KV_SERIALIZE_MAP()
  };

  struct integrated_address {
    account_public_address adr;
    crypto::hash8 payment_id;

    BEGIN_SERIALIZE_OBJECT()
    FIELD(adr)
    FIELD(payment_id)
    END_SERIALIZE()

    BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(adr)
    KV_SERIALIZE(payment_id)
    END_KV_SERIALIZE_MAP()
  };

  struct keypair
  {
    crypto::public_key pub;
    crypto::secret_key sec;

    static inline keypair generate()
    {
      keypair k;
      generate_keys(k.pub, k.sec);
      return k;
    }
  };
  //---------------------------------------------------------------

}

BLOB_SERIALIZER(cryptonote::txout_to_key);
BLOB_SERIALIZER(cryptonote::txout_to_scripthash);

VARIANT_TAG(binary_archive, cryptonote::txin_gen, 0xff);
VARIANT_TAG(binary_archive, cryptonote::txin_to_script, 0x0);
VARIANT_TAG(binary_archive, cryptonote::txin_to_scripthash, 0x1);
VARIANT_TAG(binary_archive, cryptonote::txin_to_key, 0x2);
VARIANT_TAG(binary_archive, cryptonote::txout_to_script, 0x0);
VARIANT_TAG(binary_archive, cryptonote::txout_to_scripthash, 0x1);
VARIANT_TAG(binary_archive, cryptonote::txout_to_key, 0x2);
VARIANT_TAG(binary_archive, cryptonote::transaction, 0xcc);
VARIANT_TAG(binary_archive, cryptonote::block, 0xbb);

VARIANT_TAG(json_archive, cryptonote::txin_gen, "gen");
VARIANT_TAG(json_archive, cryptonote::txin_to_script, "script");
VARIANT_TAG(json_archive, cryptonote::txin_to_scripthash, "scripthash");
VARIANT_TAG(json_archive, cryptonote::txin_to_key, "key");
VARIANT_TAG(json_archive, cryptonote::txout_to_script, "script");
VARIANT_TAG(json_archive, cryptonote::txout_to_scripthash, "scripthash");
VARIANT_TAG(json_archive, cryptonote::txout_to_key, "key");
VARIANT_TAG(json_archive, cryptonote::transaction, "tx");
VARIANT_TAG(json_archive, cryptonote::block, "block");

VARIANT_TAG(debug_archive, cryptonote::txin_gen, "gen");
VARIANT_TAG(debug_archive, cryptonote::txin_to_script, "script");
VARIANT_TAG(debug_archive, cryptonote::txin_to_scripthash, "scripthash");
VARIANT_TAG(debug_archive, cryptonote::txin_to_key, "key");
VARIANT_TAG(debug_archive, cryptonote::txout_to_script, "script");
VARIANT_TAG(debug_archive, cryptonote::txout_to_scripthash, "scripthash");
VARIANT_TAG(debug_archive, cryptonote::txout_to_key, "key");
VARIANT_TAG(debug_archive, cryptonote::transaction, "tx");
VARIANT_TAG(debug_archive, cryptonote::block, "block");
