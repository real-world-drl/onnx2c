/* This file is part of onnx2c.
 * 
 * BatchNormalization.
 * Algorithm as "described in this paper https://arxiv.org/abs/1502.03167"
 *
 * i.e. This operator provides a "whitening" of the data in the middle of
 * the network, (as opposed to just a preprocessing the input).
 * 
 * Algorithm calculated is: 
 *
 * 	y = scale * X + bias
 * where
 * 	X = (x - mean) / sqrt( variance + epsilon )
 *
 *
 * mean and variance can, optionally, be updated and sent as output,
 * but this is still unimplemented in onnx2c.
 */ 
namespace toC {

class BatchNormalization : public Node {
	public:
	BatchNormalization() {
		op_name = "BatchNormalization";
		epsilon = 1e-5;
		momentum = 0.9;
		input=scale=bias=mean=var=output=NULL;
		sqrt_var_offline = false;
	}
	bool sqrt_var_offline; // TODO: is it ever possible that we can't compute sqrt(var) offline?

	float epsilon;
	float momentum;

	// inputs
	const Tensor *input; // 'X' in the spec
	const Tensor *scale;
	const Tensor *bias;  // 'B' in the spec
	const Tensor *mean;
	const Tensor *var;
	// outputs
	const Tensor *output;
	// ... optional outputs not yet implmeneted


	void parseAttribute_epsilon( const onnx::AttributeProto &a ) {
		if( a.type() != onnx::AttributeProto_AttributeType_FLOAT )
			ERROR("Bad attribute " << a.name());
		if( a.has_f() == false )
			ERROR("Bad attribute " << a.name());
		epsilon = a.f();
	}
	void parseAttribute_momentum( const onnx::AttributeProto &a ) {
		if( a.type() != onnx::AttributeProto_AttributeType_FLOAT )
			ERROR("Bad attribute " << a.name());
		if( a.has_f() == false )
			ERROR("Bad attribute " << a.name());
		momentum = a.f();
	}

	virtual void parseAttributes( onnx::NodeProto &node ) override {

		for( const auto& a : node.attribute() ) {
			if( a.name() == "epsilon" )
				parseAttribute_epsilon(a);
			else if( a.name() == "momentum" )
				parseAttribute_momentum(a);
			else if( a.name() == "spatial" ) {
				// NB: spatial was removed in onnx opset v. 9.
				int spatial = parse_attribute_int(a);
				if( spatial != 1 )
					ERROR("non-default value for 'spatial' attribute not implemented");
			}
			else
				ERROR("Unknown attribute " << a.name());
		}
	}

	virtual void print(std::ostream &dst) const override
	{
		int batch_size =input->data_dim[0]; 
		int num_chan =input->data_dim[1]; 
		std::string type = input->data_type_str();

		dst << "\t/* BatchNormalization" << std::endl;
		dst << "\t * epsilon = " << epsilon << std::endl;
		dst << "\t * momentum = " << momentum << std::endl;
		dst << "\t */" << std::endl << std::endl;

		if( sqrt_var_offline  == false)
			INDT_1 << "float epsilon = " << epsilon << ";" <<std::endl;

		dst<<"\t" << "for( int32_t b=0; b<" << batch_size << "; b++ ) {" << std::endl;
		dst<<"\t" << "for( int32_t c=0; c<" << num_chan << "; c++ ) {" << std::endl;

		// create the indexing string for picking out an element in input/output
		std::string idxs = "[b][c]";
		for( unsigned i = 2; i<input->data_dim.size(); i++)
			idxs += "[i" + std::to_string(i) + "]";


		// Loop over data dimensions
		for( unsigned i = 2; i<input->data_dim.size(); i++) {
			std::string idx = "i" + std::to_string(i);
			dst << "\t" << "for( uint32_t " << idx << "=0; ";
			dst <<               idx << "<" << input->data_dim[i] << "; ";
			dst <<               idx <<"++ ) {" << std::endl;
		}

		INDT_2 << type << " tmp_X = ( X" << idxs << " - mean[c] ) / ";
		if( sqrt_var_offline )
			dst <<  "( var[c] );" << std::endl;
		else
			dst << "( sqrt( var[c] + epsilon));" << std::endl;

		INDT_2 << "output" << idxs << " = tmp_X ";
		if( scale )
			dst << "* scale[c]";
		if( bias )
			dst << " + bias[c]";
		dst << ";" << std::endl;

		for( unsigned i = 2; i<input->data_dim.size(); i++)
			dst << "\t}" << std::endl;

		dst<<"\t" << "}" << std::endl;
		dst<<"\t" << "}" << std::endl;
	}

	// TODO: this could be useful elsewhere too
	bool isSplatted(const Tensor *t, float value)
	{
		if( t->data_type != onnx::TensorProto_DataType_FLOAT )
			ERROR("Unimplemented");
		if( t->isConst == false )
			return false;

		float *b = (float*)t->data_buffer;

		for( int i=0; i<t->data_num_elem(); i++ )
			if( b[i] != value )
				return false;

		return true;
	}

	// Updates variance tensor in-place to contain the entire denominator
	// of the BatchNormalization formula.
	// TODO: This breaks if var is used anywere else.
	void calculateSqrtVarOffline(void)
	{
		float *v = (float*)var->data_buffer;
		for( int i=0; i<var->data_num_elem(); i++)
			v[i] = sqrt(v[i] + epsilon);
	}

	virtual void resolve(void) override
	{
		if( inputs.size() != 5 )
			ERROR("wrong number of inputs to BatchNormalization");

		input = inputs[0]; // "X"
		register_input(input, "X");
		scale = inputs[1];
		register_input(scale, "scale");
		bias = inputs[2]; // "B" in spec
		register_input(bias, "bias");
		mean = inputs[3];
		register_input(mean, "mean");
		var = inputs[4];
		register_input(var, "var");

		if( typeConstraint_plainFloatingPoints(input) == false)
			ERROR("Incorrect input for node");
		if( typeConstraint_plainFloatingPoints(scale) == false)
			ERROR("Incorrect input for node");
		if( typeConstraint_plainFloatingPoints(bias) == false)
			ERROR("Incorrect input for node");
		if( typeConstraint_plainFloatingPoints(mean) == false)
			ERROR("Incorrect input for node");
		if( typeConstraint_plainFloatingPoints(var) == false)
			ERROR("Incorrect input for node");

		// It is possible that scale is all ones, and bias is all zeros!
		// But the scale and bias tensors are not optional inputs in ONNX, so they are always
		// provided.
		if( isSplatted(scale, 1.0f) )
			scale = NULL;
		if( isSplatted(bias, 0.0f) )
			bias = NULL;
		if( var->isConst ) {
			calculateSqrtVarOffline();
			sqrt_var_offline = true;
		}

		Tensor *rv = new Tensor;
		rv->data_dim = input->data_dim;
		rv->data_type = input->data_type;
		output = rv;
		register_output(rv, "output");
	}
};
}

