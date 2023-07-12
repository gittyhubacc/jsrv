function process_message(msg)
	print('greetings:\n')
	for i, v in ipairs (msg.hello) do
		print('\thello ' .. v .. '(' .. i .. ')\n')
	end
	print('oh yeah, one more thing: ' .. msg.another)
end
