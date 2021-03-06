/************************************************************
 *
 *    Transfer Handlers
 *
 ***********************************************************/
void Transfer_validate(chain::message_validate_context& context) {
   auto transfer = context.msg.as<Transfer>();
   try {
      EOS_ASSERT(transfer.amount > Asset(0), message_validate_exception, "Must transfer a positive amount");
      EOS_ASSERT(context.msg.has_notify(transfer.to), message_validate_exception, "Must notify recipient of transfer");
   } FC_CAPTURE_AND_RETHROW((transfer))
}

void Transfer_validate_preconditions(chain::precondition_validate_context& context) {
   const auto& db = context.db;
   auto transfer = context.msg.as<Transfer>();

   db.get<account_object,by_name>(transfer.to); ///< make sure this exists
   const auto& from = db.get<account_object,by_name>(transfer.from);
   EOS_ASSERT(from.balance >= transfer.amount.amount, message_precondition_exception, "Insufficient Funds",
              ("from.balance",from.balance)("transfer.amount",transfer.amount));
}
void Transfer_apply(chain::apply_context& context) {
   auto& db = context.mutable_db;
   auto transfer = context.msg.as<Transfer>();
   const auto& from = db.get<account_object,by_name>(transfer.from);
   const auto& to = db.get<account_object,by_name>(transfer.to);
   db.modify(from, [&](account_object& a) {
      a.balance -= transfer.amount;
   });
   db.modify(to, [&](account_object& a) {
      a.balance += transfer.amount;
   });
}

/************************************************************
 *
 *    DefineStruct
 *
 ***********************************************************/
void DefineStruct_validate(chain::message_validate_context& context) {
   auto  msg = context.msg.as<DefineStruct>();
   FC_ASSERT(msg.definition.name != TypeName(), "must define a type name");
// TODO:  validate_type_name( msg.definition.name)
//   validate_type_name( msg.definition.base)
}
void DefineStruct_validate_preconditions(chain::precondition_validate_context& context) {
   auto& db = context.db;
   auto  msg = context.msg.as<DefineStruct>();
   db.get<account_object,by_name>(msg.scope);
#warning TODO:  db.get<account_object>(sg.base_scope)
}
void DefineStruct_apply(chain::apply_context& context) {
   auto& db = context.mutable_db;
   auto  msg = context.msg.as<DefineStruct>();

   db.create<type_object>( [&](auto& type) {
      type.scope = msg.scope;
      type.name  = msg.definition.name;
      type.fields.reserve(msg.definition.fields.size());
#warning TODO:  type.base_scope = 
      type.base  = msg.definition.base;
      for(const auto& f : msg.definition.fields) {
        type.fields.push_back(f);
      }
   });
}

/************************************************************
 *
 *    SetMessageHandler
 *
 ***********************************************************/
void SetMessageHandler_validate(chain::message_validate_context& context) {
   auto  msg = context.msg.as<SetMessageHandler>();
}
void SetMessageHandler_validate_preconditions(chain::precondition_validate_context& context) 
{ try {
   auto& db = context.db;
   auto  msg = context.msg.as<SetMessageHandler>();
   idump((msg.recipient)(msg.processor)(msg.type));
   // db.get<type_object,by_scope_name>( boost::make_tuple(msg.account, msg.type))

   // TODO: verify code compiles
} FC_CAPTURE_AND_RETHROW() }

void SetMessageHandler_apply(chain::apply_context& context) {
   auto& db = context.mutable_db;
   auto  msg = context.msg.as<SetMessageHandler>();
   const auto& processor_acnt = db.get<account_object,by_name>(msg.processor);
   const auto& recipient_acnt = db.get<account_object,by_name>(msg.recipient);
   db.create<action_code_object>( [&](auto& action){
       action.processor                   = processor_acnt.id;
       action.recipient                   = recipient_acnt.id;
       action.type                        = msg.type;
       action.validate_action             = msg.validate.c_str();
       action.validate_precondition       = msg.precondition.c_str();
       action.apply                       = msg.apply.c_str();
   });
   idump((msg.apply));
}




/************************************************************
 *
 *    Create Account Handlers
 *
 ***********************************************************/
///@{
void Authority_validate_preconditions(const Authority& auth, chain::precondition_validate_context& context) {
   for(const auto& a : auth.accounts)
      context.db.get<account_object,by_name>(a.permission.account);
}

void CreateAccount_validate(chain::message_validate_context& context) {
   auto create = context.msg.as<CreateAccount>();

   EOS_ASSERT( validate(create.owner), message_validate_exception, "Invalid owner authority");
   EOS_ASSERT( validate(create.active), message_validate_exception, "Invalid active authority");
   EOS_ASSERT( validate(create.recovery), message_validate_exception, "Invalid recovery authority");
}

void CreateAccount_validate_preconditions(chain::precondition_validate_context& context) {
   const auto& db = context.db;
   auto create = context.msg.as<CreateAccount>();

   db.get<account_object,by_name>(create.creator); ///< make sure it exists

   auto existing_account = db.find<account_object, by_name>(create.name);
   EOS_ASSERT(existing_account == nullptr, message_precondition_exception,
              "Cannot create account named ${name}, as that name is already taken",
              ("name", create.name));

   const auto& creator = db.get<account_object,by_name>(context.msg.sender);
   EOS_ASSERT(creator.balance >= create.deposit, message_precondition_exception, "Insufficient Funds");

#warning TODO: make sure creation deposit is greater than min account balance

   Authority_validate_preconditions(create.owner, context);
   Authority_validate_preconditions(create.active, context);
   Authority_validate_preconditions(create.recovery, context);
}

void CreateAccount_apply(chain::apply_context& context) {
   auto& db = context.mutable_db;
   auto create = context.msg.as<CreateAccount>();
   db.modify(db.get<account_object,by_name>(context.msg.sender), [&create](account_object& a) {
      a.balance -= create.deposit;
   });
   const auto& new_account = db.create<account_object>([&create](account_object& a) {
      a.name = create.name;
      a.balance = create.deposit;
   });
   const auto& owner_permission = db.create<permission_object>([&create, &new_account](permission_object& p) {
      p.name = "owner";
      p.parent = 0;
      p.owner = new_account.id;
      p.auth = std::move(create.owner);
   });
   db.create<permission_object>([&create, &owner_permission](permission_object& p) {
      p.name = "active";
      p.parent = owner_permission.id;
      p.owner = owner_permission.owner;
      p.auth = std::move(create.active);
   });
}
///@}  Create Account Handlers

/************************************************************
 *
 *    Create/Update Producer Handlers
 *
 ***********************************************************/
///@{
void CreateProducer_validate(chain::message_validate_context& context) {
   auto create = context.msg.as<CreateProducer>();
   EOS_ASSERT(create.name.size() > 0, message_validate_exception, "Producer owner name cannot be empty");
}
void CreateProducer_validate_preconditions(chain::precondition_validate_context& context) {
   auto create = context.msg.as<CreateProducer>();
   const auto& db = context.db;
   const auto& owner = db.get<account_object,by_name>(create.name);
   auto producer = db.find<producer_object, by_owner>(owner.id);
   EOS_ASSERT(producer == nullptr, message_precondition_exception,
              "Account ${name} already has a block producer", ("name", create.name));
}
void CreateProducer_apply(chain::apply_context& context) {
   auto create = context.msg.as<CreateProducer>();
   auto& db = context.mutable_db;
   const auto& owner = db.get<account_object,by_name>(create.name);
   db.create<producer_object>([&create, &owner](producer_object& p) {
      p.owner = owner.id;
      p.signing_key = create.key;
   });
}

void UpdateProducer_validate(chain::message_validate_context& context) {
   auto update = context.msg.as<UpdateProducer>();
   EOS_ASSERT(update.name.size() > 0, message_validate_exception, "Producer owner name cannot be empty");
}
void UpdateProducer_validate_preconditions(chain::precondition_validate_context& context) {
   const auto& db = context.db;
   auto update = context.msg.as<UpdateProducer>();
   const auto& producer = db.get<producer_object, by_owner>(db.get<account_object, by_name>(update.name).id);
   EOS_ASSERT(producer.signing_key != update.newKey || producer.configuration != update.configuration,
              message_validate_exception, "Producer's new settings may not be identical to old settings");
}
void UpdateProducer_apply(chain::apply_context& context) {
   auto& db = context.mutable_db;
   auto update = context.msg.as<UpdateProducer>();
   const auto& producer = db.get<producer_object, by_owner>(db.get<account_object, by_name>(update.name).id);

   db.modify(producer, [&update](producer_object& p) {
      p.signing_key = update.newKey;
      p.configuration = update.configuration;
   });
}
///@}  Create/Update Producer Handlers
